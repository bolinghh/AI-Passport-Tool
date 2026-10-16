use anyhow::{Context, Result};
use passport_core::{audio, bridge};
fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match args.first().map(String::as_str) {
        Some("devices")=>{for name in audio::outputs()?{println!("{name}");}}
        Some("inputs")=>{for name in audio::inputs()?{println!("{name}");}}
        Some("tone")=>{let client=audio::AudioClient::new();client.tone(args.get(1).context("Specify an output endpoint")?)?;std::thread::sleep(std::time::Duration::from_millis(400));}
        Some("loopback")=>println!("{}",audio::loopback(args.get(1).context("Specify output")?,args.get(2).context("Specify input")?)?),
        Some("ble-check")=>{tokio::runtime::Runtime::new()?.block_on(bridge::check_connection(args.get(1).map(String::as_str)))?;}
        _=>println!("Open Passport.app for the native Mac interface. Diagnostics: devices, inputs, tone, loopback, ble-check."),
    }
    Ok(())
}
