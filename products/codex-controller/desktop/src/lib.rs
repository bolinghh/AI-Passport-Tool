pub mod audio;
pub mod bridge;
pub mod config;
pub mod connection;
mod logic;
mod pairing;
pub mod wire;
use anyhow::{Context, Result};
use bridge::{Command, Shared};
use config::{Config, Weather};
use serde::Deserialize;
use std::{
    ffi::{CStr, CString},
    os::raw::c_char,
    sync::{Arc, Mutex},
    thread::JoinHandle,
};
use tokio::sync::mpsc;

pub struct Core {
    shared: Shared,
    sender: mpsc::UnboundedSender<Command>,
    worker: Option<JoinHandle<()>>,
    audio: Arc<audio::AudioClient>,
}
#[derive(Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
enum Request {
    Scan,
    Connect { id: String },
    Disconnect,
    Retry,
    Stop,
    Apply { config: Config },
    Time { epoch: u32, offset: i32 },
    Weather { weather: Weather },
    Tone { output: String },
    Loopback { output: String },
}
impl Core {
    pub fn start() -> Result<Self> {
        let config = Config::load()?;
        let mut connection = connection::Connection::default();
        if config.peripheral.is_empty() {
            connection.scan();
        } else {
            connection.select(&config.peripheral);
        }
        let shared = Arc::new(Mutex::new(bridge::Status {
            connection,
            config: config.clone(),
            ..Default::default()
        }));
        let (sender, rx) = mpsc::unbounded_channel();
        let state = shared.clone();
        let audio = Arc::new(audio::AudioClient::new());
        let worker_audio = audio.clone();
        let runtime = tokio::runtime::Runtime::new()?;
        let worker = std::thread::spawn(move || {
            runtime.block_on(bridge::run(config, state, rx, worker_audio))
        });
        Ok(Self {
            shared,
            sender,
            worker: Some(worker),
            audio,
        })
    }
    fn submit(&self, json: &str) -> Result<()> {
        let req: Request = serde_json::from_str(json).context("Invalid core command")?;
        let command = match req {
            Request::Scan => {
                let mut s = self.shared.lock().unwrap();
                anyhow::ensure!(!s.stopped, "后台已停止");
                s.connection.cancel();
                s.connection.idle();
                s.connection.scan();
                self.audio.stop();
                s.error.clear();
                Command::Scan
            }
            Request::Connect { id } => {
                let mut s = self.shared.lock().unwrap();
                anyhow::ensure!(!s.stopped, "后台已停止，请重启应用");
                let epoch = s.connection.select(&id).context("当前不能开始另一次连接")?;
                s.error.clear();
                self.audio.stop();
                Command::Connect(id, epoch)
            }
            Request::Retry => {
                let mut s = self.shared.lock().unwrap();
                let epoch = s.connection.retry().context("请先选择设备")?;
                self.audio.stop();
                Command::Connect(s.connection.target.clone(), epoch)
            }
            Request::Disconnect => {
                self.shared.lock().unwrap().connection.cancel();
                self.audio.stop();
                Command::Disconnect
            }
            Request::Stop => {
                self.audio.stop();
                Command::Stop
            }
            Request::Apply { config } => {
                config.validate()?;
                let s = self.shared.lock().unwrap();
                anyhow::ensure!(
                    !s.mic || s.config.output == config.output,
                    "传音期间不能切换音频输出"
                );
                Command::Apply(config)
            }
            Request::Time { epoch, offset } => {
                anyhow::ensure!(
                    epoch >= 1577836800 && (-86400..=86400).contains(&offset),
                    "Invalid clock"
                );
                Command::Time(bridge::Clock { epoch, offset })
            }
            Request::Weather { weather } => {
                weather.validate()?;
                Command::Weather(weather)
            }
            Request::Tone { output } => {
                let s = self.shared.lock().unwrap();
                anyhow::ensure!(!s.mic && !s.diagnostic, "请先停止传音或诊断");
                Command::Tone(output)
            }
            Request::Loopback { output } => {
                let s = self.shared.lock().unwrap();
                anyhow::ensure!(!s.mic && !s.diagnostic, "请先停止传音或诊断");
                Command::Loopback(output)
            }
        };
        self.sender
            .send(command)
            .context("后台已停止，请重启应用")?;
        Ok(())
    }
}
impl Drop for Core {
    fn drop(&mut self) {
        self.audio.stop();
        {
            let mut s = self.shared.lock().unwrap();
            s.pin.cancel();
            s.connection.cancel();
        }
        let _ = self.sender.send(Command::Quit);
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}
fn boundary<T>(f: impl FnOnce() -> T) -> std::thread::Result<T> {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(f))
}
fn string(value: String) -> *mut c_char {
    CString::new(value)
        .map(CString::into_raw)
        .unwrap_or(std::ptr::null_mut())
}
/// Owned handle. Call destroy once after all send/snapshot calls have finished.
#[no_mangle]
pub extern "C" fn passport_core_create() -> *mut Core {
    boundary(|| {
        Core::start()
            .map(|c| Box::into_raw(Box::new(c)))
            .unwrap_or(std::ptr::null_mut())
    })
    .unwrap_or(std::ptr::null_mut())
}
/// # Safety
/// `core` must be a live handle; `json` must be a NUL-terminated UTF-8 string.
/// The returned string must be released with passport_string_free.
#[no_mangle]
pub unsafe extern "C" fn passport_core_send(core: *mut Core, json: *const c_char) -> *mut c_char {
    let result = boundary(|| -> Result<()> {
        anyhow::ensure!(
            !core.is_null() && !json.is_null(),
            "Invalid handle or command"
        );
        (*core).submit(CStr::from_ptr(json).to_str()?)
    });
    string(match result {
        Ok(Ok(())) => String::new(),
        Ok(Err(e)) => format!("{e:#}"),
        Err(_) => "Core command failed".into(),
    })
}
/// # Safety
/// `core` must be live and not concurrently destroyed. Free the returned string.
#[no_mangle]
pub unsafe extern "C" fn passport_core_snapshot(core: *mut Core) -> *mut c_char {
    boundary(|| {
        if core.is_null() {
            return std::ptr::null_mut();
        }
        string(
            serde_json::to_string(&*(*core).shared.lock().unwrap()).unwrap_or_else(|_| "{}".into()),
        )
    })
    .unwrap_or(std::ptr::null_mut())
}
#[no_mangle]
pub extern "C" fn passport_audio_outputs() -> *mut c_char {
    boundary(|| string(serde_json::to_string(&audio::outputs().unwrap_or_default()).unwrap()))
        .unwrap_or(std::ptr::null_mut())
}
/// # Safety
/// `value` must be NULL or an unreleased string returned by this library.
#[no_mangle]
pub unsafe extern "C" fn passport_string_free(value: *mut c_char) {
    if !value.is_null() {
        drop(CString::from_raw(value));
    }
}
/// # Safety
/// `core` must be NULL or an owned live handle with no outstanding calls.
#[no_mangle]
pub unsafe extern "C" fn passport_core_destroy(core: *mut Core) {
    if !core.is_null() {
        let _ = boundary(|| drop(Box::from_raw(core)));
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn queued_old_connect_is_invalidated_by_switch_cancel_retry_and_rescan() {
        let (sender, mut rx) = mpsc::unbounded_channel();
        let shared = Arc::new(Mutex::new(bridge::Status::default()));
        let core = Core {
            shared: shared.clone(),
            sender,
            worker: None,
            audio: Arc::new(audio::AudioClient::new()),
        };
        core.submit(r#"{"type":"connect","id":"a"}"#).unwrap();
        let old = match rx.try_recv().unwrap() {
            Command::Connect(_, epoch) => epoch,
            _ => panic!(),
        };
        core.submit(r#"{"type":"connect","id":"b"}"#).unwrap();
        let next = match rx.try_recv().unwrap() {
            Command::Connect(_, epoch) => epoch,
            _ => panic!(),
        };
        {
            let s = shared.lock().unwrap();
            assert!(!s.connection.current(old));
            assert!(s.connection.current(next));
            assert_eq!(s.connection.target, "b");
        }
        core.submit(r#"{"type":"disconnect"}"#).unwrap();
        assert!(!shared.lock().unwrap().connection.current(next));
        assert!(!shared.lock().unwrap().connection.retry_enabled);
        core.submit(r#"{"type":"retry"}"#).unwrap();
        assert!(shared.lock().unwrap().connection.retry_enabled);
        core.submit(r#"{"type":"scan"}"#).unwrap();
        assert!(!shared.lock().unwrap().connection.retry_enabled);
        assert_eq!(
            shared.lock().unwrap().connection.phase,
            connection::Phase::Scanning
        );
    }
    #[test]
    fn command_admission_and_serialization_do_not_touch_hardware() {
        let (sender, mut rx) = mpsc::unbounded_channel();
        let shared = Arc::new(Mutex::new(bridge::Status::default()));
        let core = Core {
            shared: shared.clone(),
            sender,
            worker: None,
            audio: Arc::new(audio::AudioClient::new()),
        };
        core.submit(r#"{"type":"connect","id":"passport"}"#)
            .unwrap();
        assert!(core
            .submit(r#"{"type":"connect","id":"passport"}"#)
            .is_err());
        assert!(matches!(rx.try_recv(), Ok(Command::Connect(_, _))));
        assert!(core
            .submit(r#"{"type":"time","epoch":1,"offset":0}"#)
            .is_err());
        shared.lock().unwrap().mic = true;
        assert!(core
            .submit(r#"{"type":"tone","output":"BlackHole 2ch"}"#)
            .is_err());
        core.submit(r#"{"type":"stop"}"#).unwrap();
        assert!(matches!(rx.try_recv(), Ok(Command::Stop)));
        let json = serde_json::to_string(&*shared.lock().unwrap()).unwrap();
        assert!(!json.contains("sessions"));
        unsafe {
            let ptr = passport_core_snapshot(&core as *const Core as *mut Core);
            assert!(!ptr.is_null());
            passport_string_free(ptr);
        }
    }
}
