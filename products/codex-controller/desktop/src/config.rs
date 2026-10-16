use anyhow::{bail, Context, Result};
use serde::{Deserialize, Serialize};
use std::{
    fs,
    path::{Path, PathBuf},
};
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct City {
    pub name: String,
    pub latitude: f64,
    pub longitude: f64,
}
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Weather {
    pub city: String,
    pub fetched_at: u32,
    pub temperature: i16,
    pub code: u16,
}
impl Weather {
    pub fn validate(&self) -> Result<()> {
        validate_name(&self.city)?;
        anyhow::ensure!(
            self.fetched_at > 0 && (-1000..=700).contains(&self.temperature),
            "Invalid weather data"
        );
        Ok(())
    }
    pub fn message(&self) -> Result<crate::wire::Message> {
        self.validate()?;
        let mut m = crate::wire::Message::new(crate::wire::WEATHER);
        m.payload.extend(self.fetched_at.to_be_bytes());
        m.payload.extend(self.temperature.to_be_bytes());
        m.payload.extend(self.code.to_be_bytes());
        m.payload.push(self.city.len() as u8);
        m.payload.extend(self.city.as_bytes());
        Ok(m)
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct Config {
    pub output: String,
    pub peripheral: String,
    pub focus_minutes: u8,
    pub rest_minutes: u8,
    pub city: Option<City>,
    pub weather: Option<Weather>,
}
impl Default for Config {
    fn default() -> Self {
        Self {
            output: if cfg!(target_os = "macos") {
                "BlackHole 2ch"
            } else {
                "CABLE Input"
            }
            .into(),
            peripheral: String::new(),
            focus_minutes: 25,
            rest_minutes: 5,
            city: None,
            weather: None,
        }
    }
}
fn directory() -> PathBuf {
    dirs::config_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("passport-codex-controller")
}
impl Config {
    pub fn load() -> Result<Self> {
        Self::load_from(&directory())
    }
    fn load_from(dir: &Path) -> Result<Self> {
        let p = dir.join("settings.json");
        let c: Self = if p.exists() {
            serde_json::from_slice(&fs::read(p)?).context("Invalid Passport settings")?
        } else {
            #[derive(Deserialize)]
            struct Legacy {
                output: Option<String>,
                peripheral: Option<String>,
            }
            let mut c = Self::default();
            let old = dir.join("config.json");
            if old.exists() {
                let legacy: Legacy = serde_json::from_slice(&fs::read(old)?)
                    .context("Cannot read previous audio/device settings")?;
                if let Some(output) = legacy.output {
                    c.output = output;
                }
                if let Some(peripheral) = legacy.peripheral {
                    c.peripheral = peripheral;
                }
            }
            c
        };
        c.validate()?;
        Ok(c)
    }
    pub fn save(&self) -> Result<()> {
        self.save_to(&directory())
    }
    fn save_to(&self, dir: &Path) -> Result<()> {
        self.validate()?;
        fs::create_dir_all(dir)?;
        let temp = dir.join("settings.json.tmp");
        fs::write(&temp, serde_json::to_vec_pretty(self)?)?;
        fs::rename(temp, dir.join("settings.json"))?;
        Ok(())
    }
    pub fn validate(&self) -> Result<()> {
        anyhow::ensure!(!self.output.trim().is_empty(), "请选择音频输出");
        anyhow::ensure!(
            (1..=120).contains(&self.focus_minutes) && (1..=120).contains(&self.rest_minutes),
            "计时长度必须为1至120分钟"
        );
        if let Some(c) = &self.city {
            validate_name(&c.name)?;
            anyhow::ensure!(
                c.latitude.is_finite()
                    && c.longitude.is_finite()
                    && (-90.0..=90.0).contains(&c.latitude)
                    && (-180.0..=180.0).contains(&c.longitude),
                "Invalid city coordinates"
            );
        }
        if let Some(w) = &self.weather {
            w.validate()?;
            anyhow::ensure!(
                self.city.as_ref().is_some_and(|c| c.name == w.city),
                "Weather city does not match selected city"
            );
        }
        Ok(())
    }
    pub fn settings_message(&self) -> crate::wire::Message {
        let mut m = crate::wire::Message::new(crate::wire::SETTINGS_UPDATE);
        m.request = 1;
        m.payload = vec![self.focus_minutes, self.rest_minutes];
        m
    }
}
pub fn validate_name(name: &str) -> Result<()> {
    if name.trim().is_empty() || name.len() > 48 {
        bail!("城市显示名称不能为空，且最多48字节");
    }
    let coverage: Vec<u32> = serde_json::from_str(include_str!("../../font-codepoints.json"))?;
    if name
        .chars()
        .any(|c| c.is_control() || coverage.binary_search(&(c as u32)).is_err())
    {
        bail!("城市显示名称含不支持的字符，请使用中文或英文别名");
    }
    Ok(())
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn bounds_and_weather() {
        let mut c = Config {
            focus_minutes: 0,
            ..Default::default()
        };
        assert!(c.validate().is_err());
        c.focus_minutes = 120;
        assert!(c.validate().is_ok());
        assert!(validate_name("杭州").is_ok());
        assert!(validate_name("😀").is_err());
        let w = Weather {
            city: "杭州".into(),
            fetched_at: 1800000000,
            temperature: -3,
            code: 0,
        };
        assert_eq!(w.message().unwrap().payload.len(), 15);
        c.weather = Some(w);
        assert!(c.validate().is_err());
    }
    #[test]
    fn migration_only_copies_device_and_output_without_touching_old_file() {
        let dir = std::env::temp_dir().join(format!("passport-migration-{}", std::process::id()));
        fs::create_dir_all(&dir).unwrap();
        let old=br#"{"output":"BlackHole 2ch","peripheral":"test-device","sessions":[{"name":"private","url":"private"}]}"#;
        fs::write(dir.join("config.json"), old).unwrap();
        let c = Config::load_from(&dir).unwrap();
        assert_eq!(c.peripheral, "test-device");
        assert!(c.city.is_none());
        c.save_to(&dir).unwrap();
        assert_eq!(fs::read(dir.join("config.json")).unwrap(), old);
        assert!(!fs::read_to_string(dir.join("settings.json"))
            .unwrap()
            .contains("private"));
        assert_eq!(Config::load_from(&dir).unwrap().focus_minutes, 25);
        fs::remove_dir_all(dir).unwrap();
    }
}
