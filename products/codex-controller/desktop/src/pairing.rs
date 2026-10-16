use anyhow::{Context, Result};
use btleplug::api::Peripheral as _;
use btleplug::platform::Peripheral;
use std::future::Future;
use std::sync::{Arc, Mutex};

#[cfg(not(target_os = "windows"))]
pub struct PairingLease;
#[cfg(target_os = "windows")]
pub struct PairingLease {
    device: windows::Devices::Bluetooth::BluetoothLEDevice,
    session: Option<windows::Devices::Bluetooth::GenericAttributeProfile::GattSession>,
}
#[cfg(target_os = "windows")]
impl Drop for PairingLease {
    fn drop(&mut self) {
        if let Some(session) = &self.session {
            let _ = session.SetMaintainConnection(false);
            let _ = session.Close();
        }
        let _ = self.device.Close();
    }
}

#[derive(Clone, Default)]
pub struct PinPrompt(Arc<Mutex<Option<tokio::sync::oneshot::Sender<String>>>>);
impl PinPrompt {
    pub fn is_pending(&self) -> bool {
        self.0.lock().unwrap().is_some()
    }
    #[cfg(any(target_os = "windows", test))]
    fn begin(&self) -> Result<tokio::sync::oneshot::Receiver<String>> {
        let mut slot = self.0.lock().unwrap();
        anyhow::ensure!(slot.is_none(), "A pairing code request is already pending");
        let (send, receive) = tokio::sync::oneshot::channel();
        *slot = Some(send);
        Ok(receive)
    }
    pub fn submit(&self, pin: &str) -> Result<()> {
        anyhow::ensure!(
            pin.len() == 6 && pin.bytes().all(|b| b.is_ascii_digit()),
            "请输入设备当前显示的六位数字"
        );
        let send = self
            .0
            .lock()
            .unwrap()
            .take()
            .context("配对请求已结束，请重新连接")?;
        send.send(pin.to_owned())
            .map_err(|_| anyhow::anyhow!("配对请求已结束，请重新连接"))
    }
    pub fn cancel(&self) {
        self.0.lock().unwrap().take();
    }
}

#[cfg(any(target_os = "windows", test))]
struct CancelOnDrop<F: FnOnce()>(Option<F>);
#[cfg(any(target_os = "windows", test))]
impl<F: FnOnce()> Drop for CancelOnDrop<F> {
    fn drop(&mut self) {
        if let Some(cancel) = self.0.take() {
            cancel();
        }
    }
}

async fn authenticated_connection<P, F, G, L>(pair: P, connect: F) -> Result<L>
where
    P: Future<Output = Result<L>>,
    F: FnOnce() -> G,
    G: Future<Output = Result<()>>,
{
    let lease = pair.await?;
    connect().await?;
    Ok(lease)
}

pub async fn connect(peripheral: &Peripheral, prompt: Option<PinPrompt>) -> Result<PairingLease> {
    // btleplug's Windows connect includes GATT discovery. Authenticate its native
    // physical link first. Keep its lease throughout the protocol session:
    // closing the native GATT session can also invalidate backend GATT operations.
    authenticated_connection(prepare(peripheral, prompt), || async {
        peripheral
            .connect()
            .await
            .context("[connect] BLE service connection failed")
    })
    .await
}

#[cfg(not(target_os = "windows"))]
pub async fn prepare(_peripheral: &Peripheral, _prompt: Option<PinPrompt>) -> Result<PairingLease> {
    // CoreBluetooth performs OS pairing when a protected attribute is accessed.
    Ok(PairingLease)
}

#[cfg(target_os = "windows")]
async fn native_link(
    device: &windows::Devices::Bluetooth::BluetoothLEDevice,
) -> Result<windows::Devices::Bluetooth::GenericAttributeProfile::GattSession> {
    use std::time::Duration;
    use windows::Devices::Bluetooth::GenericAttributeProfile::{GattSession, GattSessionStatus};
    let physical = GattSession::FromDeviceIdAsync(&device.BluetoothDeviceId()?)?
        .await
        .context("[link] Native GATT session failed")?;
    physical.SetMaintainConnection(true)?;
    // Close even when the active-session wait is cancelled or times out.
    let mut cleanup = CancelOnDrop(Some(|| {
        let _ = physical.SetMaintainConnection(false);
        let _ = physical.Close();
    }));
    tokio::time::timeout(Duration::from_secs(15), async {
        loop {
            if physical.SessionStatus()? == GattSessionStatus::Active {
                return Ok::<(), anyhow::Error>(());
            }
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
    })
    .await
    .context("[link] Physical BLE connection timed out")??;
    cleanup.0 = None;
    drop(cleanup);
    Ok(physical)
}

#[cfg(target_os = "windows")]
pub async fn prepare(peripheral: &Peripheral, prompt: Option<PinPrompt>) -> Result<PairingLease> {
    use anyhow::bail;
    use std::time::Duration;
    use windows::Devices::{
        Bluetooth::BluetoothLEDevice,
        Enumeration::{
            DeviceAccessStatus, DeviceInformationCustomPairing, DevicePairingKinds,
            DevicePairingProtectionLevel, DevicePairingRequestedEventArgs,
            DevicePairingResultStatus,
        },
    };
    use windows::{core::HSTRING, Foundation::TypedEventHandler};

    let device = BluetoothLEDevice::FromBluetoothAddressAsync(peripheral.address().into())?
        .await
        .context("[pair] Windows device lookup failed")?;
    let mut lease = PairingLease {
        device,
        session: None,
    };
    let device = &lease.device;
    let result: Result<()> = async {
        let access = device.DeviceAccessInformation()?.CurrentStatus()?;
        if access != DeviceAccessStatus::Allowed {
            bail!("[access] Windows Bluetooth access is {access:?}");
        }
        let pairing = device.DeviceInformation()?.Pairing()?;
        if !pairing.IsPaired()? {
            let prompt = prompt.context("[pair] First pairing requires the desktop bridge code-entry dialog; pair there before running ble-check")?;
            let custom = pairing.Custom()?;
            let runtime = tokio::runtime::Handle::current();
            let requests = prompt.clone();
            let handler = TypedEventHandler::<DeviceInformationCustomPairing, DevicePairingRequestedEventArgs>::new(
                move |_sender, args| {
                    let args = args.ok()?.clone();
                    if args.PairingKind()? != DevicePairingKinds::ProvidePin { return Ok(()); }
                    let deferral = args.GetDeferral()?;
                    let receive = match requests.begin() {
                        Ok(receive) => receive,
                        Err(_) => { deferral.Complete()?; return Ok(()); }
                    };
                    runtime.spawn(async move {
                        let _complete = CancelOnDrop(Some(move || { let _ = deferral.Complete(); }));
                        if let Ok(Ok(pin)) = tokio::time::timeout(Duration::from_secs(45), receive).await {
                            let _ = args.AcceptWithPin(&HSTRING::from(pin));
                        }
                    });
                    Ok(())
                },
            );
            let token = custom.PairingRequested(&handler)?;
            let _remove = CancelOnDrop(Some(|| { let _ = custom.RemovePairingRequested(token); }));
            let _clear = CancelOnDrop(Some(|| prompt.cancel()));
            lease.session = Some(native_link(device).await?);
            let operation = custom.PairWithProtectionLevelAsync(
                DevicePairingKinds::ProvidePin,
                DevicePairingProtectionLevel::EncryptionAndAuthentication,
            )?;
            // Dropping an awaiting future does not cancel a WinRT OS pairing dialog.
            let mut cancellation = CancelOnDrop(Some(|| { let _ = operation.Cancel(); }));
            let complete = tokio::time::timeout(Duration::from_secs(60), async {
                operation.clone().await
            }).await;
            let outcome = match complete {
                Ok(result) => {
                    cancellation.0 = None;
                    result.context("[pair] Windows pairing operation failed")?
                }
                Err(_) => {
                    let _ = operation.Cancel();
                    bail!("[pair] Windows pairing timed out after 60 seconds");
                }
            };
            let status = outcome.Status()?;
            if status != DevicePairingResultStatus::Paired
                && status != DevicePairingResultStatus::AlreadyPaired
            {
                bail!("[pair] Windows pairing failed: {status:?}");
            }
            if outcome.ProtectionLevelUsed()? != DevicePairingProtectionLevel::EncryptionAndAuthentication {
                bail!("[pair] Windows pairing did not provide encryption and authentication");
            }
        } else if pairing.ProtectionLevel()? != DevicePairingProtectionLevel::EncryptionAndAuthentication {
            bail!("[pair] Saved Windows bond lacks authenticated encryption; reset both device and OS bonds");
        } else {
            // Bonded reconnects also need a maintained physical link before the
            // backend performs GATT discovery; a saved bond alone is insufficient.
            lease.session = Some(native_link(device).await?);
        }
        Ok(())
    }.await;
    result?;
    Ok(lease)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    };

    #[tokio::test]
    async fn pin_validation_does_not_consume_request_and_cancel_closes_it() {
        let prompt = PinPrompt::default();
        let wait = prompt.begin().unwrap();
        assert!(prompt.submit("12345").is_err());
        assert!(prompt.submit("１２３４５６").is_err());
        assert!(prompt.is_pending());
        prompt.cancel();
        assert!(wait.await.is_err());
        assert!(!prompt.is_pending());
        assert!(prompt.submit("123456").is_err());
    }

    #[tokio::test]
    async fn pin_is_delivered_once_and_duplicate_request_is_rejected() {
        let prompt = PinPrompt::default();
        let wait = prompt.begin().unwrap();
        assert!(prompt.begin().is_err());
        prompt.submit("123456").unwrap();
        assert_eq!(wait.await.unwrap(), "123456");
        assert!(!prompt.is_pending());
        assert!(prompt.submit("123456").is_err());
    }

    #[tokio::test]
    async fn pending_pairing_blocks_protected_access_and_preserves_error() {
        let gatt = Arc::new(AtomicBool::new(false));
        let called = gatt.clone();
        let (release, wait) = tokio::sync::oneshot::channel::<Result<()>>();
        let task = tokio::spawn(authenticated_connection(
            async { wait.await.unwrap() },
            move || {
                called.store(true, Ordering::SeqCst);
                async { Ok(()) }
            },
        ));
        tokio::task::yield_now().await;
        assert!(!gatt.load(Ordering::SeqCst));
        release
            .send(Err(anyhow::anyhow!("AuthenticationFailure: 0x80650005")))
            .unwrap();
        let error = task.await.unwrap().unwrap_err();
        assert!(error.to_string().contains("0x80650005"));
        assert!(!gatt.load(Ordering::SeqCst));
    }

    #[tokio::test]
    async fn successful_pairing_allows_one_gatt_attempt() {
        let (release, wait) = tokio::sync::oneshot::channel::<Result<()>>();
        let gatt = Arc::new(AtomicBool::new(false));
        let called = gatt.clone();
        let task = tokio::spawn(authenticated_connection(
            async { wait.await.unwrap() },
            move || {
                assert!(!called.swap(true, Ordering::SeqCst));
                async { Ok(()) }
            },
        ));
        tokio::task::yield_now().await;
        assert!(!gatt.load(Ordering::SeqCst));
        release.send(Ok(())).unwrap();
        task.await.unwrap().unwrap();
        assert!(gatt.load(Ordering::SeqCst));
    }

    #[tokio::test]
    async fn native_link_lease_survives_connection_and_protocol_session() {
        let dropped = Arc::new(AtomicBool::new(false));
        let cleanup = dropped.clone();
        let observe = dropped.clone();
        let lease = authenticated_connection(
            async {
                Ok(CancelOnDrop(Some(move || {
                    cleanup.store(true, Ordering::SeqCst);
                })))
            },
            move || async move {
                assert!(!observe.load(Ordering::SeqCst));
                tokio::task::yield_now().await;
                assert!(!observe.load(Ordering::SeqCst));
                Ok(())
            },
        )
        .await
        .unwrap();
        assert!(!dropped.load(Ordering::SeqCst));
        tokio::task::yield_now().await;
        assert!(!dropped.load(Ordering::SeqCst));
        drop(lease);
        assert!(dropped.load(Ordering::SeqCst));
    }

    #[tokio::test]
    async fn cancelling_pending_pairing_runs_os_cancellation_and_never_connects() {
        let cancelled = Arc::new(AtomicBool::new(false));
        let flag = cancelled.clone();
        let (started, wait) = tokio::sync::oneshot::channel();
        let task = tokio::spawn(authenticated_connection(
            async move {
                let _cancel = CancelOnDrop(Some(move || {
                    flag.store(true, Ordering::SeqCst);
                }));
                let _ = started.send(());
                std::future::pending::<Result<()>>().await
            },
            || async {
                panic!("GATT must not start after pairing cancellation");
            },
        ));
        wait.await.unwrap();
        task.abort();
        assert!(task.await.unwrap_err().is_cancelled());
        assert!(cancelled.load(Ordering::SeqCst));
    }
}
