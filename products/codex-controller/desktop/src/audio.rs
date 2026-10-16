use crate::wire::Sequence;
use anyhow::{bail, Context, Result};
use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use std::{
    collections::VecDeque,
    sync::{
        atomic::{AtomicBool, AtomicU32, Ordering},
        mpsc, Arc, Mutex,
    },
    thread,
};

const RATE: f64 = 16000.0;
const TARGET: usize = 1920;
const CAPACITY: usize = 3840;
pub fn outputs() -> Result<Vec<String>> {
    Ok(cpal::default_host()
        .output_devices()?
        .filter_map(|d| d.name().ok())
        .collect())
}
pub fn inputs() -> Result<Vec<String>> {
    Ok(cpal::default_host()
        .input_devices()?
        .filter_map(|d| d.name().ok())
        .collect())
}

fn record<T: cpal::SizedSample + Copy + 'static>(
    device: &cpal::Device,
    config: &cpal::StreamConfig,
    samples: Arc<Mutex<Vec<f32>>>,
    failed: Arc<AtomicBool>,
    convert: fn(T) -> f32,
) -> Result<cpal::Stream> {
    let channels = config.channels as usize;
    let limit = config.sample_rate.0 as usize * 4;
    Ok(device.build_input_stream(
        config,
        move |data: &[T], _| {
            if let Ok(mut captured) = samples.try_lock() {
                for frame in data.chunks_exact(channels) {
                    if captured.len() == limit {
                        break;
                    }
                    captured.push(frame.iter().map(|s| convert(*s)).sum::<f32>() / channels as f32);
                }
            }
        },
        move |_| failed.store(true, Ordering::SeqCst),
        None,
    )?)
}

/// Explicitly selected virtual input only; no default microphone or saved recording.
pub fn loopback(output: &str, input: &str) -> Result<String> {
    let devices: Vec<_> = cpal::default_host()
        .input_devices()?
        .filter(|d| {
            d.name()
                .map(|n| n == input || n.contains(input))
                .unwrap_or(false)
        })
        .collect();
    if input.is_empty() || devices.len() != 1 {
        bail!("Select a unique virtual audio input: {input}");
    }
    let supported = devices[0].default_input_config()?;
    let config: cpal::StreamConfig = supported.clone().into();
    if config.sample_rate.0 < 16000 || config.channels == 0 {
        bail!("Unsupported virtual input format");
    }
    let samples = Arc::new(Mutex::new(Vec::with_capacity(
        config.sample_rate.0 as usize * 4,
    )));
    let failed = Arc::new(AtomicBool::new(false));
    let recorder = match supported.sample_format() {
        cpal::SampleFormat::F32 => record(
            &devices[0],
            &config,
            samples.clone(),
            failed.clone(),
            |s: f32| s,
        ),
        cpal::SampleFormat::I16 => record(
            &devices[0],
            &config,
            samples.clone(),
            failed.clone(),
            |s: i16| s as f32 / 32768.0,
        ),
        cpal::SampleFormat::U16 => record(
            &devices[0],
            &config,
            samples.clone(),
            failed.clone(),
            |s: u16| (s as f32 - 32768.0) / 32768.0,
        ),
        other => bail!("Unsupported virtual input sample format: {other:?}"),
    }?;
    recorder.play()?;
    thread::sleep(std::time::Duration::from_millis(100));
    let client = AudioClient::new();
    client.tone(output)?;
    thread::sleep(std::time::Duration::from_millis(1500));
    client.stop();
    drop(recorder);
    if failed.load(Ordering::SeqCst) {
        bail!("Virtual input stream failed");
    }
    let data = samples.lock().unwrap();
    let rate = config.sample_rate.0 as usize;
    if data.len() < rate {
        bail!("Virtual input did not provide enough samples");
    }
    let start = data
        .iter()
        .position(|s| s.abs() > 0.01)
        .context("No test tone received")?;
    let end = data.iter().rposition(|s| s.abs() > 0.01).unwrap();
    if end - start < rate / 20 {
        bail!("Test tone was too short");
    }
    let crossings = data[start..=end]
        .windows(2)
        .filter(|s| s[0] <= 0.0 && s[1] > 0.0)
        .count();
    let pitch = crossings as f64 * rate as f64 / (end - start) as f64;
    let tail = data[data.len() - rate / 2..]
        .iter()
        .fold(0.0f32, |p, s| p.max(s.abs()));
    if !(950.0..=1050.0).contains(&pitch) || tail > 0.001 {
        bail!("Loopback failed: pitch {pitch:.1} Hz, final silence peak {tail:.6}");
    }
    Ok(format!("Loopback PASS: {} Hz, {} channels, {:?}, tone {pitch:.1} Hz, final 500 ms silence peak {tail:.6}",
        config.sample_rate.0, config.channels, supported.sample_format()))
}
#[derive(Default)]
pub struct Samples {
    pub queue: VecDeque<f32>,
    fraction: f64,
    integral: f64,
    primed: bool,
}
impl Samples {
    fn push(&mut self, pcm: &[i16]) {
        self.queue.extend(pcm.iter().map(|s| *s as f32 / 32768.0));
        if self.queue.len() > CAPACITY {
            let n = self.queue.len() - TARGET;
            self.queue.drain(..n);
            self.fraction = 0.0;
        }
    }
    fn clear(&mut self) {
        *self = Self::default();
    }
    fn next(&mut self, rate: u32) -> f32 {
        if !self.primed {
            if self.queue.len() < TARGET {
                return 0.0;
            }
            self.primed = true;
        }
        if self.queue.len() < 2 {
            self.primed = false;
            self.fraction = 0.0;
            return 0.0;
        }
        // Slow feedback corrects the independent ES8311 and desktop audio clocks.
        let error = (self.queue.len() as f64 - TARGET as f64) / TARGET as f64;
        self.integral = (self.integral + error * 0.00000001).clamp(-0.003, 0.003);
        let correction = (error * 0.001 + self.integral).clamp(-0.005, 0.005);
        let out = self.queue[0] + (self.queue[1] - self.queue[0]) * self.fraction as f32;
        self.fraction += RATE / rate as f64 * (1.0 + correction);
        while self.fraction >= 1.0 && !self.queue.is_empty() {
            self.queue.pop_front();
            self.fraction -= 1.0;
        }
        out
    }
}
enum AudioCommand {
    Start(String, u32, mpsc::SyncSender<Result<()>>),
    Stop,
    Packet(u32, u16, Vec<u8>),
    Tone(String, mpsc::SyncSender<Result<()>>),
    Quit,
}
#[derive(Default)]
pub struct AudioStats {
    pub received: AtomicU32,
    pub decoded: AtomicU32,
    pub dropped: AtomicU32,
    pub decode_errors: AtomicU32,
}
impl AudioStats {
    fn reset(&self) {
        for counter in [
            &self.received,
            &self.decoded,
            &self.dropped,
            &self.decode_errors,
        ] {
            counter.store(0, Ordering::Relaxed);
        }
    }
}
pub struct AudioClient {
    pub stats: Arc<AudioStats>,
    sender: mpsc::SyncSender<AudioCommand>,
    active: Arc<AtomicU32>,
    allowed: Arc<AtomicU32>,
    pub failed: Arc<AtomicBool>,
    pub level: Arc<AtomicU32>,
}
impl Default for AudioClient {
    fn default() -> Self {
        Self::new()
    }
}
impl AudioClient {
    pub fn new() -> Self {
        let (sender, rx) = mpsc::sync_channel(16);
        let active = Arc::new(AtomicU32::new(0));
        let allowed = Arc::new(AtomicU32::new(0));
        let failed = Arc::new(AtomicBool::new(false));
        let level = Arc::new(AtomicU32::new(0));
        let stats = Arc::new(AudioStats::default());
        let worker_stats = stats.clone();
        let a = active.clone();
        let permission = allowed.clone();
        let f = failed.clone();
        let l = level.clone();
        thread::spawn(move || audio_thread(rx, a, permission, f, l, worker_stats));
        Self {
            stats,
            sender,
            active,
            allowed,
            failed,
            level,
        }
    }
    pub fn start(&self, output: &str, stream: u32) -> Result<()> {
        let result = Self::finish_start(self.prepare_start(output, stream)?);
        if result.is_err() {
            self.stop();
        }
        result
    }
    // Publish permission and enqueue under the connection-generation lock, then
    // release that lock before waiting for CoreAudio. Cancellation can close the
    // atomic callback gate even while route creation is still pending.
    pub(crate) fn prepare_start(
        &self,
        output: &str,
        stream: u32,
    ) -> Result<mpsc::Receiver<Result<()>>> {
        self.stop();
        let (tx, rx) = mpsc::sync_channel(1);
        self.allowed.store(stream, Ordering::SeqCst);
        if let Err(e) = self
            .sender
            .try_send(AudioCommand::Start(output.into(), stream, tx))
        {
            self.stop();
            return Err(e.into());
        }
        Ok(rx)
    }
    pub(crate) fn finish_start(rx: mpsc::Receiver<Result<()>>) -> Result<()> {
        rx.recv_timeout(std::time::Duration::from_secs(2))
            .context("Audio route setup timed out")?
    }
    pub fn stop(&self) {
        // The callback gate closes immediately, even if the worker has a packet backlog.
        self.allowed.store(0, Ordering::SeqCst);
        self.active.store(0, Ordering::SeqCst);
        self.level.store(0, Ordering::Relaxed);
        let _ = self.sender.try_send(AudioCommand::Stop);
    }
    pub fn packet(&self, stream: u32, seq: u16, data: Vec<u8>) {
        if permitted(
            self.active.load(Ordering::SeqCst),
            self.allowed.load(Ordering::SeqCst),
        ) && self.allowed.load(Ordering::SeqCst) == stream
        {
            self.stats.received.fetch_add(1, Ordering::Relaxed);
            if self
                .sender
                .try_send(AudioCommand::Packet(stream, seq, data))
                .is_err()
            {
                self.stats.dropped.fetch_add(1, Ordering::Relaxed);
            }
        }
    }
    pub fn tone(&self, output: &str) -> Result<()> {
        self.stop();
        let (tx, rx) = mpsc::sync_channel(1);
        self.allowed.store(u32::MAX, Ordering::SeqCst);
        if let Err(e) = self.sender.try_send(AudioCommand::Tone(output.into(), tx)) {
            self.stop();
            return Err(e.into());
        }
        rx.recv_timeout(std::time::Duration::from_secs(2))??;
        Ok(())
    }
}
impl Drop for AudioClient {
    fn drop(&mut self) {
        self.stop();
        let _ = self.sender.try_send(AudioCommand::Quit);
    }
}
fn permitted(published: u32, allowed: u32) -> bool {
    published != 0 && published == allowed
}
fn output_stream(
    output: &str,
    samples: Arc<Mutex<Samples>>,
    active: Arc<AtomicU32>,
    allowed: Arc<AtomicU32>,
    failed: Arc<AtomicBool>,
) -> Result<cpal::Stream> {
    let devices: Vec<_> = cpal::default_host()
        .output_devices()?
        .filter(|d| {
            d.name()
                .map(|n| n == output || n.contains(output))
                .unwrap_or(false)
        })
        .collect();
    if output.is_empty() || devices.len() != 1 {
        bail!("请安装并选择唯一的虚拟音频输出设备：{output}");
    }
    let device = &devices[0];
    let supported = device.default_output_config()?;
    let config: cpal::StreamConfig = supported.clone().into();
    if config.sample_rate.0 < 16000 || config.channels == 0 {
        bail!("虚拟音频设备格式不受支持");
    }
    match supported.sample_format() {
        cpal::SampleFormat::F32 => build::<f32>(device, &config, samples, active, allowed, failed),
        cpal::SampleFormat::I16 => build::<i16>(device, &config, samples, active, allowed, failed),
        cpal::SampleFormat::U16 => build::<u16>(device, &config, samples, active, allowed, failed),
        other => bail!("Unsupported output sample format: {other:?}"),
    }
}
fn build<T: cpal::SizedSample + cpal::FromSample<f32>>(
    device: &cpal::Device,
    config: &cpal::StreamConfig,
    samples: Arc<Mutex<Samples>>,
    active: Arc<AtomicU32>,
    allowed: Arc<AtomicU32>,
    failed: Arc<AtomicBool>,
) -> Result<cpal::Stream> {
    let channels = config.channels as usize;
    let rate = config.sample_rate.0;
    let error_active = active.clone();
    let stream = device.build_output_stream(
        config,
        move |data: &mut [T], _| {
            let Ok(mut source) = samples.try_lock() else {
                data.fill(T::from_sample(0.0));
                return;
            };
            for frame in data.chunks_mut(channels) {
                let value = if !permitted(
                    active.load(Ordering::SeqCst),
                    allowed.load(Ordering::SeqCst),
                ) {
                    source.clear();
                    0.0
                } else {
                    source.next(rate)
                };
                frame.fill(T::from_sample(value));
            }
        },
        move |_| {
            error_active.store(0, Ordering::SeqCst);
            failed.store(true, Ordering::SeqCst);
        },
        None,
    )?;
    stream.play()?;
    Ok(stream)
}
fn audio_thread(
    rx: mpsc::Receiver<AudioCommand>,
    active: Arc<AtomicU32>,
    allowed: Arc<AtomicU32>,
    failed: Arc<AtomicBool>,
    level: Arc<AtomicU32>,
    stats: Arc<AudioStats>,
) {
    let samples = Arc::new(Mutex::new(Samples::default()));
    let mut stream: Option<cpal::Stream> = None;
    let mut decoder: Option<opus::Decoder> = None;
    let mut seq = Sequence::default();
    let mut output_name = String::new();
    let mut pcm = [0i16; 960];
    while let Ok(command) = rx.recv() {
        match command {
            AudioCommand::Start(name, id, reply) => {
                let result = (|| -> Result<()> {
                    if name != output_name || stream.is_none() || failed.load(Ordering::SeqCst) {
                        stream = None;
                        stream = Some(output_stream(
                            &name,
                            samples.clone(),
                            active.clone(),
                            allowed.clone(),
                            failed.clone(),
                        )?);
                        output_name = name;
                    }
                    decoder = Some(opus::Decoder::new(16000, opus::Channels::Mono)?);
                    samples.lock().unwrap().clear();
                    seq.reset();
                    stats.reset();
                    failed.store(false, Ordering::SeqCst);
                    active.store(id, Ordering::SeqCst);
                    Ok(())
                })();
                if result.is_err() {
                    active.store(0, Ordering::SeqCst);
                }
                // A canceled setup must not reopen the microphone later.
                if reply.send(result).is_err() {
                    active.store(0, Ordering::SeqCst);
                    samples.lock().unwrap().clear();
                }
            }
            AudioCommand::Stop => {
                samples.lock().unwrap().clear();
                decoder = None;
                seq.reset();
            }
            AudioCommand::Packet(id, n, bytes) => {
                if id == 0
                    || active.load(Ordering::SeqCst) != id
                    || allowed.load(Ordering::SeqCst) != id
                {
                    continue;
                }
                let Some(d) = decoder.as_mut() else {
                    continue;
                };
                let Some(missing) = seq.accept(n) else {
                    continue;
                };
                if missing > 3 {
                    let _ = d.reset_state();
                    samples.lock().unwrap().clear();
                }
                for _ in 0..missing.min(3) {
                    if let Ok(count) = d.decode(&[], &mut pcm, false) {
                        samples.lock().unwrap().push(&pcm[..count]);
                    }
                }
                match d.decode(&bytes, &mut pcm, false) {
                    Ok(count) if count == 960 => {
                        stats.decoded.fetch_add(1, Ordering::Relaxed);
                        let peak = pcm
                            .iter()
                            .map(|v| (*v as i32).unsigned_abs())
                            .max()
                            .unwrap_or(0);
                        level.store(peak * 100 / 32768, Ordering::Relaxed);
                        samples.lock().unwrap().push(&pcm[..count]);
                    }
                    _ => {
                        stats.decode_errors.fetch_add(1, Ordering::Relaxed);
                        active.store(0, Ordering::SeqCst);
                        failed.store(true, Ordering::SeqCst);
                    }
                }
            }
            AudioCommand::Tone(name, reply) => {
                stream = None;
                let result = output_stream(
                    &name,
                    samples.clone(),
                    active.clone(),
                    allowed.clone(),
                    failed.clone(),
                );
                match result {
                    Ok(s) => {
                        stream = Some(s);
                        output_name = name;
                        samples.lock().unwrap().clear();
                        let tone: Vec<i16> = (0..3200)
                            .map(|i| {
                                ((i as f32 * 2.0 * std::f32::consts::PI * 1000.0 / 16000.0).sin()
                                    * 5000.0) as i16
                            })
                            .collect();
                        {
                            let mut queue = samples.lock().unwrap();
                            queue.push(&tone);
                            // Open while holding the callback's lock so it cannot clear the tone.
                            active.store(u32::MAX, Ordering::SeqCst);
                        }
                        if reply.send(Ok(())).is_err() {
                            active.store(0, Ordering::SeqCst);
                            samples.lock().unwrap().clear();
                            continue;
                        }
                        thread::sleep(std::time::Duration::from_millis(350));
                        active.store(0, Ordering::SeqCst);
                    }
                    Err(e) => {
                        let _ = reply.send(Err(e));
                    }
                }
            }
            AudioCommand::Quit => break,
        }
    }
    active.store(0, Ordering::SeqCst);
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn cancelled_setup_cannot_reopen_a_permission_closed_output() {
        assert!(permitted(7, 7));
        assert!(!permitted(7, 0)); // An already queued setup publishes after stop.
        assert!(!permitted(7, 8)); // A previous stream cannot enter a new route.
        assert!(!permitted(0, 0));
    }
    #[test]
    fn pending_route_can_be_cancelled_before_worker_ack_without_reopening_audio() {
        let (sender, receiver) = mpsc::sync_channel(8);
        let client = AudioClient {
            stats: Arc::new(AudioStats::default()),
            sender,
            active: Arc::new(AtomicU32::new(0)),
            allowed: Arc::new(AtomicU32::new(0)),
            failed: Arc::new(AtomicBool::new(false)),
            level: Arc::new(AtomicU32::new(0)),
        };
        let pending = client.prepare_start("mock route", 7).unwrap();
        assert!(matches!(receiver.try_recv(), Ok(AudioCommand::Stop)));
        let ack = match receiver.try_recv().unwrap() {
            AudioCommand::Start(_, 7, ack) => ack,
            _ => panic!(),
        };
        // No worker acknowledgement exists yet; control is already free to stop.
        client.stop();
        client.active.store(7, Ordering::SeqCst);
        ack.send(Ok(())).unwrap();
        AudioClient::finish_start(pending).unwrap();
        assert!(!permitted(
            client.active.load(Ordering::SeqCst),
            client.allowed.load(Ordering::SeqCst)
        ));
        client.packet(7, 1, vec![1]);
        assert_eq!(client.stats.received.load(Ordering::Relaxed), 0);
    }
    #[test]
    fn packet_diagnostics_count_backpressure_without_saving_audio() {
        let (sender, _receiver) = mpsc::sync_channel(1);
        let stats = Arc::new(AudioStats::default());
        let client = AudioClient {
            stats: stats.clone(),
            sender,
            active: Arc::new(AtomicU32::new(7)),
            allowed: Arc::new(AtomicU32::new(7)),
            failed: Arc::new(AtomicBool::new(false)),
            level: Arc::new(AtomicU32::new(0)),
        };
        client.packet(6, 0, vec![1]); // A stale stream never enters the worker.
        client.packet(7, 1, vec![1]);
        client.packet(7, 2, vec![2]); // Full queue is observable.
        assert_eq!(stats.received.load(Ordering::Relaxed), 2);
        assert_eq!(stats.dropped.load(Ordering::Relaxed), 1);
        client.stop();
        client.packet(7, 3, vec![3]);
        assert_eq!(stats.received.load(Ordering::Relaxed), 2);
        stats.reset();
        assert_eq!(stats.received.load(Ordering::Relaxed), 0);
        assert_eq!(stats.dropped.load(Ordering::Relaxed), 0);
    }
    #[test]
    fn route_backpressure_keeps_output_closed() {
        let (sender, _receiver) = mpsc::sync_channel(1);
        assert!(sender.try_send(AudioCommand::Stop).is_ok());
        let active = Arc::new(AtomicU32::new(42));
        let client = AudioClient {
            stats: Arc::new(AudioStats::default()),
            sender,
            active: active.clone(),
            allowed: Arc::new(AtomicU32::new(42)),
            failed: Arc::new(AtomicBool::new(false)),
            level: Arc::new(AtomicU32::new(100)),
        };
        // A stalled audio worker must not block BLE control or leave its gate open.
        assert!(client.start("unavailable", 43).is_err());
        assert_eq!(active.load(Ordering::SeqCst), 0);
        assert!(client.tone("unavailable").is_err());
        assert_eq!(client.level.load(Ordering::Relaxed), 0);
    }
    #[test]
    fn opus_roundtrip_and_packet_loss() {
        let mut enc =
            opus::Encoder::new(16000, opus::Channels::Mono, opus::Application::Voip).unwrap();
        enc.set_bitrate(opus::Bitrate::Bits(16000)).unwrap();
        enc.set_complexity(0).unwrap();
        enc.set_vbr(false).unwrap();
        let pcm: Vec<i16> = (0..960)
            .map(|i| ((i as f64 * std::f64::consts::TAU * 440.0 / RATE).sin() * 8000.0) as i16)
            .collect();
        let mut encoded = [0; 256];
        let n = enc.encode(&pcm, &mut encoded).unwrap();
        assert_eq!(n, 120);
        let mut dec = opus::Decoder::new(16000, opus::Channels::Mono).unwrap();
        let mut decoded = [0; 960];
        assert_eq!(dec.decode(&encoded[..n], &mut decoded, false).unwrap(), 960);
        assert!(decoded.iter().any(|s| s.unsigned_abs() > 1000));
        assert_eq!(dec.decode(&[], &mut decoded, false).unwrap(), 960);
        assert_eq!(dec.decode(&encoded[..n], &mut decoded, false).unwrap(), 960);
    }
    #[test]
    fn bounded_drift_and_flush() {
        let mut b = Samples::default();
        b.push(&vec![1000; 960]);
        assert_eq!(b.next(48000), 0.0);
        b.push(&vec![1000; 960]);
        assert!(b.next(48000) > 0.0);
        for _ in 0..1000 {
            b.push(&vec![1000; 960]);
            for _ in 0..2880 {
                assert!(b.next(48000).is_finite());
            }
            assert!(b.queue.len() <= CAPACITY);
        }
        b.clear();
        for _ in 0..2880 {
            assert_eq!(b.next(48000), 0.0);
        }
    }
    #[test]
    fn resampling_frequency() {
        let mut b = Samples::default();
        let pcm: Vec<_> = (0..3840)
            .map(|i| ((i as f64 * std::f64::consts::TAU * 1000.0 / RATE).sin() * 10000.0) as i16)
            .collect();
        b.push(&pcm);
        let out: Vec<_> = (0..4800).map(|_| b.next(48000)).collect();
        let crossings = out.windows(2).filter(|w| w[0] <= 0.0 && w[1] > 0.0).count();
        assert!((99..=101).contains(&crossings));
    }
}
