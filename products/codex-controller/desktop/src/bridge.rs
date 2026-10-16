use crate::{
    audio::AudioClient,
    config::{Config, Weather},
    connection::{Connection, Phase},
    logic::Ledger,
    wire::*,
};
use anyhow::{bail, Context, Result};
use btleplug::{
    api::{
        Central, CentralEvent, CentralState, Characteristic, Manager as _, Peripheral as _,
        ScanFilter, WriteType,
    },
    platform::{Adapter, Manager, Peripheral},
};
use futures::StreamExt;
use std::{
    sync::{atomic::Ordering, Arc, Mutex},
    time::{Duration, Instant},
};
use tokio::sync::mpsc;
use uuid::Uuid;

const SERVICE: Uuid = Uuid::from_u128(0x6e400001_b5a3_f393_e0a9_e50e24dcca9e);
const RX: Uuid = Uuid::from_u128(0x6e400002_b5a3_f393_e0a9_e50e24dcca9e);
const TX: Uuid = Uuid::from_u128(0x6e400003_b5a3_f393_e0a9_e50e24dcca9e);
#[derive(Clone, Debug, serde::Serialize)]
pub struct Device {
    pub id: String,
    pub name: String,
}
#[derive(Default, serde::Serialize)]
pub struct Status {
    #[serde(skip)]
    pub pin: crate::pairing::PinPrompt,
    pub text: String,
    pub error: String,
    pub devices: Vec<Device>,
    pub connected: bool,
    pub connection: Connection,
    pub mic: bool,
    pub battery: Option<i8>,
    pub rssi: Option<i8>,
    pub page: u8,
    pub timer: TimerStatus,
    pub config: Config,
    pub clock: Option<Clock>,
    pub diagnostic: bool,
    pub diagnostic_result: String,
    pub level: u32,
    pub audio_packets: u32,
    pub decoded_packets: u32,
    pub dropped_packets: u32,
    pub decode_errors: u32,
    pub quit: bool,
    pub stopped: bool,
}
#[derive(Clone, Default, serde::Serialize)]
pub struct TimerStatus {
    pub rest: bool,
    pub state: u8,
    pub remaining: u32,
    pub completion: u32,
}
#[derive(Clone, serde::Serialize)]
pub struct Clock {
    pub epoch: u32,
    pub offset: i32,
}
impl Clock {
    fn message(&self) -> Message {
        let mut m = Message::new(TIME);
        m.payload.extend(self.epoch.to_be_bytes());
        m.payload.extend(self.offset.to_be_bytes());
        m
    }
}
pub enum Command {
    Scan,
    Connect(String, u64),
    Disconnect,
    Apply(Config),
    Tone(String),
    Loopback(String),
    Stop,
    Time(Clock),
    Weather(Weather),
    Quit,
}
pub type Shared = Arc<Mutex<Status>>;
fn status(shared: &Shared, text: &str) {
    let mut s = shared.lock().unwrap();
    s.text = text.into();
}
async fn write(p: &Peripheral, c: &Characteristic, m: &Message) -> Result<()> {
    let bytes = m.encode()?;
    // 20-byte writes also work on platforms where the negotiated MTU is unavailable.
    for (index, chunk) in bytes.chunks(20).enumerate() {
        tokio::time::timeout(
            Duration::from_secs(2),
            p.write(c, chunk, WriteType::WithResponse),
        )
        .await
        .with_context(|| {
            format!(
                "[write] message type {} fragment {index} timed out after 2 seconds",
                m.kind
            )
        })?
        .with_context(|| format!("[write] message type {} fragment {index} failed", m.kind))?;
    }
    Ok(())
}

/// Bounded live diagnostic: no saved configuration, session launch or audio capture.
pub async fn check_connection(target: Option<&str>) -> Result<()> {
    let manager = Manager::new().await.context("BLE manager")?;
    let adapter = manager
        .adapters()
        .await?
        .into_iter()
        .next()
        .context("No Bluetooth adapter is available")?;
    println!("[scan] Searching for Passport for 10 seconds");
    adapter.start_scan(ScanFilter::default()).await?;
    tokio::time::sleep(Duration::from_secs(10)).await;
    adapter.stop_scan().await?;
    let mut matches = Vec::new();
    let observed = adapter.peripherals().await?;
    println!(
        "[scan] Adapter {:?}; observed {} advertisers",
        adapter.adapter_state().await?,
        observed.len()
    );
    for p in observed {
        if let Some(properties) = p.properties().await? {
            if target.is_some_and(|id| p.id().to_string().eq_ignore_ascii_case(id)) {
                println!(
                    "[scan] Target name={:?} services={:?} rssi={:?}",
                    properties.local_name, properties.services, properties.rssi
                );
            }
            if properties.services.contains(&SERVICE)
                && target.is_none_or(|id| p.id().to_string().eq_ignore_ascii_case(id))
            {
                matches.push(p);
            }
        }
    }
    if matches.len() != 1 {
        bail!("[scan] Expected one Passport, found {}; specify its scan identifier if multiple are present", matches.len());
    }
    let p = matches.remove(0);
    let result: Result<()> = async {
        println!("[pair] Checking authenticated bond before GATT subscription");
        let _lease = tokio::time::timeout(Duration::from_secs(90), crate::pairing::connect(&p, None))
            .await
            .context("[connect] 90-second timeout")?
            .map_err(|e| anyhow::anyhow!("[connect] {e} ({e:?})"))?;
        println!("[services] Connected; discovering NUS");
        tokio::time::timeout(Duration::from_secs(10), p.discover_services())
            .await
            .context("[services] timeout")?
            .context("[services] discovery failed")?;
        let chars = p.characteristics();
        let rx = chars
            .iter()
            .find(|c| c.uuid == RX)
            .context("[services] missing RX")?;
        let tx = chars
            .iter()
            .find(|c| c.uuid == TX)
            .context("[services] missing TX")?;
        let mut notifications = p.notifications().await?;
        println!("[subscribe] Enabling protected notifications");
        tokio::time::timeout(Duration::from_secs(60), p.subscribe(tx))
            .await
            .context("[subscribe] timeout")?
            .context("[subscribe] failed")?;
        println!("[hello] Sending protocol handshake");
        write(&p, rx, &Message::new(HELLO))
            .await
            .context("[hello] write failed")?;
        let mut parser = Parser::default();
        tokio::time::timeout(Duration::from_secs(7), async {
            while let Some(n) = notifications.next().await {
                if n.uuid == TX
                    && parser
                        .feed(&n.value)
                        .iter()
                        .any(|m| m.kind == HELLO && m.payload.is_empty())
                {
                    return Ok::<(), anyhow::Error>(());
                }
            }
            bail!("[hello] notification stream ended");
        })
        .await
        .context("[hello] response timeout")??;
        let mut caps=Message::new(CAPABILITIES);caps.payload=vec![2,15];write(&p,rx,&caps).await?;
        tokio::time::timeout(Duration::from_secs(5),async {
            while let Some(n)=notifications.next().await {
                if n.uuid!=TX {continue;}
                for m in parser.feed(&n.value) {
                    if m.kind==CAPABILITIES {
                        anyhow::ensure!(supports_apps(&m),"Incompatible firmware; upgrade Passport firmware and Mac app");
                        return Ok::<(),anyhow::Error>(());
                    }
                }
            }
            bail!("Capability notification stream ended");
        }).await.context("Multi-app negotiation timed out; update firmware")??;
        println!("[hello] PASS: authenticated multi-app v2 handshake; microphone stayed off");
        println!("[heartbeat] Checking 30 seconds of bidirectional idle traffic");
        let until = Instant::now() + Duration::from_secs(30);
        let mut last_rx = Instant::now();
        let mut heartbeat = tokio::time::interval(Duration::from_secs(3));
        while Instant::now() < until {
            tokio::select! {
                notification = notifications.next() => {
                    let n = notification.context("[heartbeat] notification stream ended")?;
                    if n.uuid == TX && parser.feed(&n.value).iter().any(|m| m.kind == HEARTBEAT && m.payload.is_empty()) {
                        last_rx = Instant::now();
                    }
                }
                _ = heartbeat.tick() => {
                    anyhow::ensure!(last_rx.elapsed() < Duration::from_secs(7), "[heartbeat] response timed out after 7 seconds");
                    write(&p, rx, &Message::new(HEARTBEAT)).await?;
                }
            }
        }
        println!("[heartbeat] PASS: idle connection stayed responsive for 30 seconds");
        Ok(())
    }
    .await;
    let disconnect = tokio::time::timeout(Duration::from_secs(5), p.disconnect()).await;
    if matches!(&disconnect, Ok(Ok(()))) {
        println!("[cleanup] Diagnostic connection released");
    } else {
        eprintln!("[cleanup] Disconnect failed or timed out");
    }
    result?;
    disconnect.context("[cleanup] disconnect timeout")??;
    Ok(())
}
fn unix_seconds() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}
#[derive(Debug)]
struct UpgradeRequired;
impl std::fmt::Display for UpgradeRequired {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("协议不兼容，请更新 Passport 固件与 Mac 应用")
    }
}
impl std::error::Error for UpgradeRequired {}
fn terminal(error: &anyhow::Error) -> bool {
    error.is::<UpgradeRequired>() || error.chain().any(|e| {
        matches!(e.downcast_ref::<btleplug::Error>(), Some(btleplug::Error::PermissionDenied))
        // CoreBluetooth exposes pairing denial as a platform error, not PermissionDenied.
        || { let s=e.to_string().to_lowercase(); ["pairing rejected", "pairing was rejected", "authentication failure", "authentication failed", "insufficient authentication", "not authorized", "permission denied"].iter().any(|needle| s.contains(needle)) }
    })
}
/// Only observations from this scan qualify. Adapter.peripherals() includes stale objects
/// on CoreBluetooth; asking it for an old identifier can leave connect() unresolved.
async fn fresh_scan(
    adapter: &Adapter,
    shared: &Shared,
    target: Option<&str>,
) -> Result<Option<Peripheral>> {
    let mut events = adapter.events().await?;
    tokio::time::timeout(
        Duration::from_secs(3),
        adapter.start_scan(ScanFilter::default()),
    )
    .await
    .context("扫描启动超时")??;
    let observations = events.by_ref().map(|event| {
        Ok(match event {
            CentralEvent::DeviceDiscovered(id)
            | CentralEvent::DeviceUpdated(id)
            | CentralEvent::ServicesAdvertisement { id, .. }
            | CentralEvent::ManufacturerDataAdvertisement { id, .. }
            | CentralEvent::ServiceDataAdvertisement { id, .. } => Some(id),
            CentralEvent::StateUpdate(CentralState::PoweredOff) => bail!("蓝牙已关闭"),
            _ => None,
        })
    });
    discover_current(observations, target, shared, |id| async move {
        let p = adapter.peripheral(&id).await?;
        let Some(prop) = p.properties().await? else {
            return Ok(None);
        };
        if !prop.services.contains(&SERVICE) {
            return Ok(None);
        };
        Ok(Some((
            Device {
                id: id.to_string(),
                name: prop.local_name.unwrap_or_else(|| "Passport".into()),
            },
            p,
        )))
    })
    .await
}
// The resolver is never called for cached IDs: only this round's advertisements
// can yield a connection candidate. Kept independent from the BLE implementation
// so delayed discovery, stale handles and scan errors can be exercised on a host.
async fn discover_current<Id, P, S, F, Fut>(
    observations: S,
    target: Option<&str>,
    shared: &Shared,
    mut resolve: F,
) -> Result<Option<P>>
where
    S: futures::Stream<Item = Result<Option<Id>>>,
    F: FnMut(Id) -> Fut,
    Fut: std::future::Future<Output = Result<Option<(Device, P)>>>,
{
    tokio::pin!(observations);
    let until = tokio::time::sleep(Duration::from_secs(3));
    tokio::pin!(until);
    let mut items = Vec::<Device>::new();
    shared.lock().unwrap().devices.clear();
    loop {
        tokio::select! {
            _=&mut until=>break,
            event=observations.next()=>{
                let Some(event)=event else {bail!("蓝牙事件流已关闭")};
                let Some(id)=event? else {continue};
                if let Some((item,p))=tokio::time::timeout(Duration::from_secs(1),resolve(id)).await.context("发现设备读取超时")?? {
                    let matches=target==Some(item.id.as_str());
                    if !items.iter().any(|d|d.id==item.id) {items.push(item);}
                    shared.lock().unwrap().devices=items.clone();
                    if matches {return Ok(Some(p));}
                }
            }
        }
    }
    Ok(None)
}

async fn adapter_open() -> Result<Adapter> {
    tokio::time::timeout(Duration::from_secs(10), async {
        Manager::new()
            .await?
            .adapters()
            .await?
            .into_iter()
            .next()
            .context("找不到蓝牙适配器，请开启蓝牙")
    })
    .await
    .context("蓝牙初始化超时，请检查系统蓝牙权限")?
}
async fn cleanup(adapter: &Adapter, p: Option<&Peripheral>, connecting: bool) -> Result<()> {
    tokio::time::timeout(Duration::from_secs(5), async {
        adapter.stop_scan().await?;
        if let Some(p) = p {
            // A disconnected CoreBluetooth peripheral has already been removed internally;
            // calling disconnect again on that handle can wait forever in btleplug 0.12.
            if connecting || p.is_connected().await? {
                p.disconnect().await?;
            }
        }
        Ok::<_, anyhow::Error>(())
    })
    .await
    .context("蓝牙清理超时（5 秒），可以立即重试")?
}
pub async fn run(
    mut config: Config,
    shared: Shared,
    mut commands: mpsc::UnboundedReceiver<Command>,
    audio: Arc<AudioClient>,
) {
    shared.lock().unwrap().config = config.clone();
    let mut adapter: Option<Adapter> = None;
    // Initial intent is installed before Core is exposed to Swift. Never replace a
    // newer connect/cancel issued while this worker was starting.
    let mut pending = {
        let s = shared.lock().unwrap();
        if s.connection.retry_enabled {
            Some(Command::Connect(
                s.connection.target.clone(),
                s.connection.epoch(),
            ))
        } else if s.connection.phase == Phase::Scanning {
            Some(Command::Scan)
        } else {
            None
        }
    };
    let mut paired_ids = std::collections::HashSet::new();
    if !config.peripheral.is_empty() {
        paired_ids.insert(config.peripheral.clone());
    }
    let mut due: Option<tokio::time::Instant> = None;
    let mut scan_only = false;
    loop {
        if let Some(command) = pending.take() {
            let result: Result<()> = match command {
                Command::Quit => break,
                Command::Disconnect => {
                    due = None;
                    scan_only = false;
                    {
                        let mut s = shared.lock().unwrap();
                        s.connection.idle();
                        s.text = "连接已断开；自动重连已停止，麦克风保持关闭".into();
                    }
                    Ok(())
                }
                Command::Scan => {
                    due = Some(tokio::time::Instant::now());
                    scan_only = true;
                    Ok(())
                }
                Command::Connect(id, epoch) => {
                    if shared.lock().unwrap().connection.current(epoch) {
                        config.peripheral = id;
                        shared.lock().unwrap().config = config.clone();
                        due = Some(tokio::time::Instant::now());
                        scan_only = false;
                        config.save()
                    } else {
                        Ok(())
                    }
                }
                Command::Apply(mut c) => {
                    c.peripheral = config.peripheral.clone();
                    config = c;
                    shared.lock().unwrap().config = config.clone();
                    config.save()
                }
                Command::Time(clock) => {
                    shared.lock().unwrap().clock = Some(clock);
                    Ok(())
                }
                Command::Weather(w) => save_weather(&mut config, &shared, w),
                Command::Stop => {
                    audio.stop();
                    Ok(())
                }
                Command::Tone(output) => audio.tone(&output),
                Command::Loopback(output) => {
                    diagnose_audio(&shared, output);
                    Ok(())
                }
            };
            if let Err(e) = result {
                shared.lock().unwrap().error = format!("{e:#}");
            }
        }
        let deadline =
            due.unwrap_or_else(|| tokio::time::Instant::now() + Duration::from_secs(86400));
        tokio::select! {
            biased;
            command=commands.recv()=> {pending=Some(command.unwrap_or(Command::Quit));continue;},
            _=tokio::time::sleep_until(deadline), if due.is_some()=>{},
        }
        due = None;
        if adapter.is_none() {
            let opening = adapter_open();
            tokio::pin!(opening);
            tokio::select! {
                biased;
                command=commands.recv()=>{pending=Some(command.unwrap_or(Command::Quit));due=Some(tokio::time::Instant::now());continue;},
                result=&mut opening=>match result {
                    Ok(a)=>adapter=Some(a),
                    Err(e)=> {retry_after(&shared,&mut due,&e);continue;},
                }
            }
        }
        let a = adapter.as_ref().unwrap();
        // Poll state as well as listening for events: a powered-off adapter need not
        // emit another event to a newly subscribed observer.
        match tokio::time::timeout(Duration::from_secs(2), a.adapter_state()).await {
            Ok(Ok(CentralState::PoweredOn)) => {}
            Ok(Err(e)) => {
                retry_after(&shared, &mut due, &e.into());
                continue;
            }
            _ => {
                let mut s = shared.lock().unwrap();
                s.connection.phase = Phase::BluetoothOff;
                s.text = "蓝牙已关闭；开启后将继续连接".into();
                due = Some(tokio::time::Instant::now() + Duration::from_secs(1));
                continue;
            }
        }
        let (target, epoch) = {
            let mut s = shared.lock().unwrap();
            if scan_only {
                s.connection.phase = Phase::Scanning;
            } else {
                if !s.connection.retry_enabled {
                    continue;
                }
                s.connection.phase = Phase::Connecting;
            }
            s.connection.next_retry_at = None;
            s.text = if scan_only {
                "正在扫描 BLE 设备"
            } else {
                "正在重连 Passport，麦克风保持关闭"
            }
            .into();
            (s.connection.target.clone(), s.connection.epoch())
        };
        let mut physical: Option<Peripheral> = None;
        let mut connecting = false;
        let outcome = async {
            // This stream is subscribed before connect, so setup disconnects cannot be lost.
            let mut events=a.events().await?;
            let work=async {
                let found = {
                    let scanning=fresh_scan(a,&shared,if scan_only{None}else{Some(&target)});
                    tokio::pin!(scanning);
                    tokio::select! {
                        biased;
                        command=commands.recv()=>return Ok(SessionEnd::Control(command.unwrap_or(Command::Quit))),
                        result=&mut scanning=>result?,
                    }
                };
                // Stop scanning before connecting to avoid a discovery callback replacing
                // the handle we have just acquired.
                tokio::time::timeout(Duration::from_secs(2),a.stop_scan()).await.context("扫描停止超时")??;
                if scan_only {return Ok(SessionEnd::Scanned);}
                physical=Some(found.context("未发现原设备，等待 Passport 开机或靠近")?);
                let p=physical.as_ref().unwrap();
                let prompt=shared.lock().unwrap().pin.clone();
                let paired=paired_ids.contains(&target);
                connecting=true;
                let connect=tokio::time::timeout(Duration::from_secs(if paired {10}else{90}),crate::pairing::connect(p,Some(prompt)));
                tokio::pin!(connect);
                let _lease=tokio::select! {
                    biased;
                    command=commands.recv()=>return Ok(SessionEnd::Control(command.unwrap_or(Command::Quit))),
                    result=&mut connect=>result.context("蓝牙连接超时，可以重试")??,
                };
                connecting=false;
                if !shared.lock().unwrap().connection.set(epoch,Phase::Syncing) {return Ok(SessionEnd::Superseded);}
                status(&shared,"正在同步设置、时间与天气");
                session(p,&mut config,&shared,&audio,&mut commands,SessionOptions{epoch,paired}).await
            };
            tokio::pin!(work);
            let mut guard=tokio::time::interval(Duration::from_millis(25));
            loop {
                tokio::select! {
                    biased;
                    _=guard.tick()=>{
                        if !scan_only && !shared.lock().unwrap().connection.current(epoch) {break Ok(SessionEnd::Superseded);}
                    },
                    event=events.next()=>match event.context("蓝牙事件流已关闭")? {
                        CentralEvent::DeviceDisconnected(id) | CentralEvent::DeviceServicesModified(id)
                            if id.to_string()==target && matches!(shared.lock().unwrap().connection.phase,Phase::Syncing|Phase::Connected)=>break Err(anyhow::anyhow!("设备已断开，正在等待重新连接")),
                        CentralEvent::StateUpdate(CentralState::PoweredOff)=>break Err(anyhow::anyhow!("蓝牙已关闭")),
                        _=>{},
                    },
                    result=&mut work=>break result,
                }
            }
        }.await;
        // Drop all old session futures/notification subscriptions before closing output.
        audio.stop();
        {
            let mut s = shared.lock().unwrap();
            if s.connected {
                paired_ids.insert(target.clone());
            }
            s.pin.cancel();
            s.connected = false;
            s.mic = false;
            s.level = 0;
            s.battery = None;
            s.rssi = None;
        }
        // The worker remains alive even if cleanup fails. New commands are queued;
        // audio and generation invalidation already happened synchronously on submit.
        if let Err(e) = cleanup(a, physical.as_ref(), connecting).await {
            shared.lock().unwrap().error = format!("{e:#}");
        }
        match outcome {
            Ok(SessionEnd::Control(command)) => {
                pending = Some(command);
                due = Some(tokio::time::Instant::now());
            }
            Ok(SessionEnd::Scanned) => {
                shared.lock().unwrap().connection.scanned();
                status(&shared, "请选择设备并连接");
            }
            Ok(SessionEnd::Superseded) => {}
            Err(e) => {
                if shared.lock().unwrap().connection.epoch() == epoch {
                    retry_after(&shared, &mut due, &e);
                }
            }
        }
    }
    audio.stop();
    let mut s = shared.lock().unwrap();
    s.connection.cancel();
    s.connection.idle();
    s.connected = false;
    s.mic = false;
    s.level = 0;
    s.quit = true;
    s.stopped = true;
}
fn retry_after(shared: &Shared, due: &mut Option<tokio::time::Instant>, error: &anyhow::Error) {
    let mut s = shared.lock().unwrap();
    s.error = format!("{error:#}");
    if terminal(error) {
        s.connection.cancel();
        s.connection.phase = Phase::Failed;
        s.text = "请检查蓝牙权限、配对或版本，然后点击立即重试".into();
        *due = None;
    } else if s.connection.retry_enabled {
        let delay = Connection::delay(s.connection.retry_count);
        s.connection.wait(unix_seconds());
        s.text = "等待设备；麦克风保持关闭".into();
        *due = Some(tokio::time::Instant::now() + Duration::from_secs(delay));
    } else {
        s.connection.phase = Phase::Failed;
        *due = None;
    }
}
enum SessionEnd {
    Control(Command),
    Scanned,
    Superseded,
}
struct SessionOptions {
    epoch: u64,
    paired: bool,
}
async fn session(
    p: &Peripheral,
    config: &mut Config,
    shared: &Shared,
    audio: &AudioClient,
    commands: &mut mpsc::UnboundedReceiver<Command>,
    options: SessionOptions,
) -> Result<SessionEnd> {
    let SessionOptions { epoch, paired } = options;
    let (rx, mut notifications) = {
        let setup = async {
            tokio::time::timeout(Duration::from_secs(10), p.discover_services())
                .await
                .context("发现 NUS 服务超时")?
                .context("发现 NUS 服务失败")?;
            let chars = p.characteristics();
            let rx = chars
                .iter()
                .find(|c| c.uuid == RX)
                .context("NUS RX characteristic is missing")?;
            let tx = chars
                .iter()
                .find(|c| c.uuid == TX)
                .context("NUS TX characteristic is missing")?;
            let notifications = p.notifications().await?;
            // Windows authentication completes before connecting; CoreBluetooth may pair here.
            // The desktop never stores a passkey.
            tokio::time::timeout(
                Duration::from_secs(if paired { 10 } else { 60 }),
                p.subscribe(tx),
            )
            .await
            .context("加密通知订阅超时；请检查系统配对提示或重试")?
            .context("加密通知订阅失败")?;
            write(p, rx, &Message::new(HELLO))
                .await
                .context("发送版本握手失败")?;
            Ok::<_, anyhow::Error>((rx.clone(), notifications))
        };
        tokio::pin!(setup);
        loop {
            tokio::select! {
                result = &mut setup => break result?,
                command = commands.recv() => match command {
                    Some(Command::Quit) | None => return Ok(SessionEnd::Control(Command::Quit)),
                    Some(Command::Disconnect) => return Ok(SessionEnd::Control(Command::Disconnect)),
                    Some(Command::Apply(mut c)) => { c.validate()?; c.peripheral=config.peripheral.clone(); *config = c; config.save()?; shared.lock().unwrap().config=config.clone(); }
                    Some(Command::Time(clock)) => {shared.lock().unwrap().clock=Some(clock);}
                    Some(Command::Weather(w)) => {save_weather(config,shared,w)?;}
                    Some(command @ (Command::Connect(_,_) | Command::Scan)) => return Ok(SessionEnd::Control(command)),
                    _ => {}
                }
            }
        }
    };
    let rx = &rx;
    let mut parser = Parser::default();
    let mut ledger = Ledger::default();
    let mut negotiated = false;
    let mut synced = false;
    let mut settings_id = 1u32;
    let mut awaiting_settings = Some((settings_id, Instant::now()));
    let started = Instant::now();
    let mut active_stream = 0;
    let mut stop_id = 0u32;
    let mut stop_wait: Option<(u32, Instant)> = None;
    let mut last_rx = Instant::now();
    let mut heartbeat = tokio::time::interval(Duration::from_secs(1));
    let mut last_heartbeat = Instant::now();
    loop {
        tokio::select! {
            notification=notifications.next()=>{
                let n=notification.context("BLE notification stream ended")?;
                if n.uuid!=TX {continue;}
                for m in parser.feed(&n.value) {
                    match m.kind {
                        HELLO if m.payload.is_empty()=>{
                            last_rx=Instant::now();
                            if !negotiated {let mut caps=Message::new(CAPABILITIES);caps.payload=vec![2,15];write(p,rx,&caps).await?;}
                        }
                        CAPABILITIES=>{
                            if !supports_apps(&m) {return Err(UpgradeRequired.into());}
                            if !negotiated {
                                negotiated=true;last_rx=Instant::now();
                                write(p,rx,&config.settings_message()).await?;
                                let clock=shared.lock().unwrap().clock.clone();
                                if let Some(clock)=clock {write(p,rx,&clock.message()).await?;}
                                if let Some(w)=&config.weather {write(p,rx,&w.message()?).await?;}
                            }
                        }
                        SETTINGS_ACK if negotiated && m.payload.len()==1=>{
                            if awaiting_settings.as_ref().is_some_and(|(id,_)|*id==m.request) {
                                anyhow::ensure!(m.payload[0]==0,"设备拒绝计时设置");
                                awaiting_settings=None;synced=true;last_rx=Instant::now();
                                let mut s=shared.lock().unwrap();if !s.connection.set(epoch,Phase::Connected) {return Ok(SessionEnd::Superseded);} s.connection.recovered();
                                s.connected=true;s.error.clear();s.text="Passport 已就绪".into();s.config=config.clone();
                            }
                        }
                        HEARTBEAT if m.payload.is_empty()=>{last_rx=Instant::now();}
                        MIC if synced && m.payload.len()==1 && m.request!=0=>{
                            last_rx=Instant::now();
                            match ledger.cached(&m) {
                                Ok(Some(reply))=>{write(p,rx,&reply).await?;continue;}
                                Err(())=>{let mut e=Message::new(ERROR);e.request=m.request;write(p,rx,&e).await?;continue;}
                                Ok(None)=>{}
                            }
                            let mut reply=Message::new(MIC_ACK);reply.request=m.request;reply.stream=m.stream;
                            reply.payload=vec![0,m.payload[0]];
                            if m.payload[0]==1 && m.stream==m.request && active_stream==0 && !shared.lock().unwrap().diagnostic && stop_wait.is_none() {
                                let pending={
                                    let s=shared.lock().unwrap();
                                    if !s.connection.current(epoch) {return Ok(SessionEnd::Superseded);}
                                    audio.prepare_start(&config.output,m.stream)
                                };
                                let result=match pending {
                                    Ok(pending)=>tokio::task::spawn_blocking(move||AudioClient::finish_start(pending)).await.context("音频路由准备任务失败")?,
                                    Err(e)=>Err(e),
                                };
                                let mut s=shared.lock().unwrap();
                                if !s.connection.current(epoch) {audio.stop();return Ok(SessionEnd::Superseded);}
                                match result {
                                    Ok(())=>{active_stream=m.stream;s.mic=true;s.error.clear();}
                                    Err(e)=>{audio.stop();reply.payload[0]=1;s.error=format!("{e:#}");}
                                }
                            } else if m.payload[0]==0 {
                                audio.stop();active_stream=0;shared.lock().unwrap().mic=false;
                            } else {reply.payload[0]=1;}
                            ledger.remember(m,reply.clone());write(p,rx,&reply).await?;
                        }
                        AUDIO if synced && m.stream==active_stream && active_stream!=0 && m.payload.len()<=256=>{
                            last_rx=Instant::now();audio.packet(m.stream,m.sequence,m.payload);
                        }
                        DEVICE_STATUS if synced && m.payload.len()==4=>{
                            last_rx=Instant::now();let mut s=shared.lock().unwrap();
                            s.battery=if m.payload[0]<=100 {Some(m.payload[0] as i8)}else{None};
                            s.rssi=if m.payload[1]==127{None}else{Some(m.payload[1] as i8)};
                            s.page=m.payload[2];
                        }
                        TIMER_STATUS if synced && m.payload.len()==10=>{
                            last_rx=Instant::now();let mut s=shared.lock().unwrap();
                            s.timer=TimerStatus{rest:m.payload[0]!=0,state:m.payload[1],remaining:u32::from_be_bytes(m.payload[2..6].try_into().unwrap()),completion:u32::from_be_bytes(m.payload[6..10].try_into().unwrap())};
                        }
                        STOP_ACK if m.payload.is_empty()=>{
                            if stop_wait.as_ref().is_some_and(|(id,_)|*id==m.request) {stop_wait=None;last_rx=Instant::now();}
                        }
                        ERROR=>{audio.stop();active_stream=0;shared.lock().unwrap().mic=false;last_rx=Instant::now();}
                        _=>{}
                    }
                }
            }
            command=commands.recv()=>match command {
                Some(Command::Quit)|None=>return Ok(SessionEnd::Control(Command::Quit)),
                Some(Command::Disconnect)=>return Ok(SessionEnd::Control(Command::Disconnect)),
                Some(Command::Stop)=>{audio.stop();active_stream=0;shared.lock().unwrap().mic=false;stop_id=stop_id.wrapping_add(1).max(1);let mut stop=Message::new(STOP);stop.request=stop_id;
                    stop_wait=Some((stop_id,Instant::now()));write(p,rx,&stop).await?;}
                Some(Command::Apply(mut c))=>{
                    c.validate()?;c.peripheral=config.peripheral.clone();
                    if active_stream!=0 && c.output!=config.output {shared.lock().unwrap().error="传音期间不能切换输出".into();continue;}
                    *config=c;config.save()?;shared.lock().unwrap().config=config.clone();
                    if negotiated {settings_id=settings_id.wrapping_add(1).max(1);let mut m=config.settings_message();m.request=settings_id;
                        write(p,rx,&m).await?;awaiting_settings=Some((settings_id,Instant::now()));}
                }
                Some(Command::Time(clock))=>{
                    shared.lock().unwrap().clock=Some(clock.clone());if negotiated {write(p,rx,&clock.message()).await?;}
                }
                Some(Command::Weather(w))=>{
                    save_weather(config,shared,w.clone())?;if negotiated {write(p,rx,&w.message()?).await?;}
                }
                Some(Command::Tone(output))=>{if active_stream==0 && !shared.lock().unwrap().diagnostic {if let Err(e)=audio.tone(&output){shared.lock().unwrap().error=format!("{e:#}");}}}
                Some(Command::Loopback(output))=>{if active_stream==0 {diagnose_audio(shared,output);}}
                Some(command @ (Command::Scan | Command::Connect(_, _)))=>return Ok(SessionEnd::Control(command))
            },
            _=heartbeat.tick()=>{
                if !synced && started.elapsed()>Duration::from_secs(7) {return Err(UpgradeRequired.into());}
                if awaiting_settings.as_ref().is_some_and(|(_,at)|at.elapsed()>Duration::from_secs(7)) {bail!("设备设置同步超时");}
                if stop_wait.as_ref().is_some_and(|(_,at)|at.elapsed()>Duration::from_secs(3)){bail!("停止传音未获设备确认，已断开连接");}
                if last_rx.elapsed()>Duration::from_secs(7) {bail!("BLE heartbeat timed out");}
                if last_heartbeat.elapsed()>=Duration::from_secs(3) {write(p,rx,&Message::new(HEARTBEAT)).await?;last_heartbeat=Instant::now();}
                if audio.failed.swap(false,Ordering::SeqCst) {
                    audio.stop();active_stream=0;stop_id=stop_id.wrapping_add(1).max(1);let mut stop=Message::new(STOP);stop.request=stop_id;
                    stop_wait=Some((stop_id,Instant::now()));write(p,rx,&stop).await?;
                    let mut s=shared.lock().unwrap();s.mic=false;s.error="音频路由失效，麦克风已关闭".into();
                }
                let mut s=shared.lock().unwrap();s.level=if active_stream==0{0}else{audio.level.load(Ordering::Relaxed)};
                s.audio_packets=audio.stats.received.load(Ordering::Relaxed);s.decoded_packets=audio.stats.decoded.load(Ordering::Relaxed);
                s.dropped_packets=audio.stats.dropped.load(Ordering::Relaxed);s.decode_errors=audio.stats.decode_errors.load(Ordering::Relaxed);
            }
        }
    }
}
fn save_weather(config: &mut Config, shared: &Shared, w: Weather) -> Result<()> {
    w.validate()?;
    anyhow::ensure!(
        config.city.as_ref().is_some_and(|c| c.name == w.city),
        "天气城市已改变，请重新刷新"
    );
    config.weather = Some(w);
    config.save()?;
    shared.lock().unwrap().config = config.clone();
    Ok(())
}
fn diagnose_audio(shared: &Shared, output: String) {
    {
        let mut s = shared.lock().unwrap();
        if s.diagnostic {
            return;
        }
        s.diagnostic = true;
        s.diagnostic_result.clear();
    }
    let shared = shared.clone();
    std::thread::spawn(move || {
        let result = crate::audio::loopback(&output, &output);
        let mut s = shared.lock().unwrap();
        s.diagnostic = false;
        match result {
            Ok(message) => s.diagnostic_result = message,
            Err(e) => s.diagnostic_result = format!("录回失败：{e:#}"),
        }
    });
}

fn supports_apps(message: &Message) -> bool {
    message.kind == CAPABILITIES && message.payload == [2, 15]
}
#[cfg(test)]
mod protocol_tests {
    use super::*;
    #[test]
    fn old_or_partial_capabilities_never_mark_ready() {
        assert!(!supports_apps(&Message::new(HELLO)));
        for payload in [vec![], vec![1, 15], vec![2, 1], vec![2, 15, 0]] {
            let mut m = Message::new(CAPABILITIES);
            m.payload = payload;
            assert!(!supports_apps(&m));
        }
        let mut m = Message::new(CAPABILITIES);
        m.payload = vec![2, 15];
        assert!(supports_apps(&m));
    }
}

#[cfg(test)]
mod recovery_tests {
    use super::*;
    use futures::stream;
    fn shared() -> Shared {
        Arc::new(Mutex::new(Status::default()))
    }
    #[tokio::test(start_paused = true)]
    async fn cached_object_is_never_resolved_without_a_current_advertisement() {
        let shared = shared();
        let result = discover_current(
            stream::pending::<Result<Option<&str>>>(),
            Some("p"),
            &shared,
            |_| async {
                panic!("must not fetch a cached peripheral");
                #[allow(unreachable_code)]
                Ok::<Option<(Device, u32)>, anyhow::Error>(None)
            },
        )
        .await
        .unwrap();
        assert_eq!(result, None);
    }
    #[tokio::test(start_paused = true)]
    async fn delayed_discovery_resolves_new_generation_after_cache_removal() {
        let shared = shared();
        for generation in [2, 3] {
            let events = stream::once(async {
                tokio::time::sleep(Duration::from_secs(2)).await;
                Ok(Some("p"))
            });
            let p = discover_current(events, Some("p"), &shared, |id| async move {
                Ok(Some((
                    Device {
                        id: id.into(),
                        name: "Passport".into(),
                    },
                    generation,
                )))
            })
            .await
            .unwrap();
            assert_eq!(p, Some(generation));
        }
    }
    #[tokio::test(start_paused = true)]
    async fn scan_failure_does_not_poison_next_scan_or_disable_commands() {
        let shared = shared();
        shared.lock().unwrap().connection.select("p");
        let failed = discover_current(
            stream::iter([Err(anyhow::anyhow!("scan failed"))]),
            Some("p"),
            &shared,
            |_: &str| async { Ok::<Option<(Device, u8)>, anyhow::Error>(None) },
        )
        .await
        .unwrap_err();
        let mut due = None;
        retry_after(&shared, &mut due, &failed);
        assert!(due.is_some());
        assert!(!shared.lock().unwrap().stopped);
        let found = discover_current(
            stream::iter([Ok(Some("p"))]),
            Some("p"),
            &shared,
            |id| async move {
                Ok(Some((
                    Device {
                        id: id.into(),
                        name: "Passport".into(),
                    },
                    7u8,
                )))
            },
        )
        .await
        .unwrap();
        assert_eq!(found, Some(7));
    }
    #[test]
    fn manual_cancel_and_terminal_errors_never_schedule_recovery() {
        let shared = shared();
        shared.lock().unwrap().connection.select("p");
        let mut due = None;
        retry_after(&shared, &mut due, &UpgradeRequired.into());
        assert!(due.is_none());
        assert!(!shared.lock().unwrap().connection.retry_enabled);
        shared.lock().unwrap().connection.retry();
        retry_after(&shared, &mut due, &btleplug::Error::PermissionDenied.into());
        assert!(due.is_none());
        shared.lock().unwrap().connection.retry();
        shared.lock().unwrap().connection.cancel();
        retry_after(
            &shared,
            &mut due,
            &anyhow::anyhow!("late heartbeat timeout"),
        );
        assert!(due.is_none());
    }
}
