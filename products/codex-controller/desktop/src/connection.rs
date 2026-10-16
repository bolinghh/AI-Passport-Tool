#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, serde::Serialize)]
pub enum Phase {
    #[default]
    Idle,
    Scanning,
    Waiting,
    BluetoothOff,
    Connecting,
    Syncing,
    Connected,
    Disconnecting,
    Failed,
}
impl Phase {
    pub fn busy(self) -> bool {
        matches!(
            self,
            Self::Scanning | Self::Connecting | Self::Syncing | Self::Disconnecting
        )
    }
    pub fn label(self) -> &'static str {
        match self {
            Self::Idle => "等待连接",
            Self::Waiting => "等待设备",
            Self::BluetoothOff => "蓝牙已关闭",
            Self::Scanning => "正在扫描",
            Self::Connecting => "正在连接",
            Self::Syncing => "正在同步",
            Self::Connected => "已连接",
            Self::Disconnecting => "正在断开",
            Self::Failed => "连接未完成",
        }
    }
}

#[derive(Clone, Debug, Default, serde::Serialize)]
pub struct Connection {
    pub phase: Phase,
    pub target: String,
    epoch: u64,
    pub retry_enabled: bool,
    pub retry_count: u32,
    pub next_retry_at: Option<u64>,
}
impl Connection {
    pub fn scan(&mut self) -> bool {
        if self.phase.busy() || self.phase == Phase::Connected {
            return false;
        }
        self.epoch = self.epoch.wrapping_add(1);
        self.phase = Phase::Scanning;
        true
    }
    pub fn scanned(&mut self) {
        if self.phase == Phase::Scanning {
            self.phase = Phase::Idle;
        }
    }
    pub fn connect(&mut self, target: &str) -> Option<u64> {
        if target.is_empty() || self.phase.busy() || self.phase == Phase::Connected {
            return None;
        }
        self.epoch = self.epoch.wrapping_add(1);
        self.target = target.into();
        self.phase = Phase::Connecting;
        Some(self.epoch)
    }
    pub fn current(&self, epoch: u64) -> bool {
        self.epoch == epoch
            && matches!(
                self.phase,
                Phase::Connecting | Phase::Syncing | Phase::Connected
            )
    }
    pub fn set(&mut self, epoch: u64, phase: Phase) -> bool {
        if !self.current(epoch) {
            return false;
        }
        self.phase = phase;
        true
    }
    pub fn epoch(&self) -> u64 {
        self.epoch
    }
    pub fn select(&mut self, target: &str) -> Option<u64> {
        if target.is_empty() || (self.target == target && self.phase.busy()) {
            return None;
        }
        self.cancel();
        self.target = target.into();
        self.retry_enabled = true;
        self.retry_count = 0;
        self.phase = Phase::Connecting;
        Some(self.epoch)
    }
    pub fn retry(&mut self) -> Option<u64> {
        if self.target.is_empty() {
            return None;
        }
        let target = self.target.clone();
        self.cancel();
        self.idle();
        self.select(&target)
    }
    pub fn wait(&mut self, now: u64) {
        self.epoch = self.epoch.wrapping_add(1);
        self.phase = Phase::Waiting;
        self.next_retry_at = Some(now + Self::delay(self.retry_count));
        self.retry_count = self.retry_count.saturating_add(1);
    }
    pub fn delay(count: u32) -> u64 {
        [1, 2, 4, 8, 15][count.min(4) as usize]
    }
    pub fn recovered(&mut self) {
        self.retry_count = 0;
        self.next_retry_at = None;
    }
    pub fn cancel(&mut self) {
        self.retry_enabled = false;
        self.next_retry_at = None;
        self.epoch = self.epoch.wrapping_add(1);
        self.phase = Phase::Disconnecting;
    }
    pub fn idle(&mut self) {
        self.phase = Phase::Idle;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn invalid_target_and_disconnect_cleanup_gate_requests() {
        let mut state = Connection::default();
        assert_eq!(state.connect(""), None);
        let epoch = state.connect("passport").unwrap();
        assert_eq!(state.target, "passport");
        assert!(state.set(epoch, Phase::Syncing));
        state.cancel();
        assert!(!state.scan());
        assert_eq!(state.connect("another"), None);
        assert!(!state.set(epoch, Phase::Connected));
        state.idle();
        assert!(state.scan());
        state.scanned();
        assert!(state.connect("another").is_some());
    }
    #[test]
    fn startup_and_repeated_clicks_create_only_one_attempt() {
        let mut state = Connection::default();
        let first = state.connect("passport").unwrap();
        for _ in 0..20 {
            assert_eq!(state.connect("passport"), None);
        }
        assert!(state.current(first));
        assert!(state.set(first, Phase::Syncing));
        assert_eq!(state.connect("passport"), None);
        assert!(state.set(first, Phase::Connected));
        assert_eq!(state.connect("passport"), None);
        assert_eq!(state.connect("another"), None);
    }
    #[test]
    fn cancelled_and_old_completions_never_change_a_new_attempt() {
        let mut state = Connection::default();
        let old = state.connect("passport").unwrap();
        state.cancel();
        assert!(!state.set(old, Phase::Connected));
        assert!(!state.set(old, Phase::Failed));
        state.idle();
        let next = state.connect("passport").unwrap();
        assert_ne!(old, next);
        assert!(!state.set(old, Phase::Failed));
        assert_eq!(state.phase, Phase::Connecting);
        assert!(state.set(next, Phase::Connected));
    }
    #[test]
    fn scan_blocks_connect_and_only_failure_or_idle_allows_retry() {
        let mut state = Connection::default();
        assert!(state.scan());
        assert!(!state.scan());
        assert_eq!(state.connect("passport"), None);
        state.scanned();
        let first = state.connect("passport").unwrap();
        assert!(state.set(first, Phase::Failed));
        let retry = state.connect("passport").unwrap();
        assert!(!state.current(first));
        assert!(state.current(retry));
    }
}

#[cfg(test)]
mod recovery_tests {
    use super::*;
    #[test]
    fn retry_deadlines_are_capped_and_reset_only_on_ready() {
        let mut c = Connection::default();
        c.select("p").unwrap();
        for (i, delay) in [1, 2, 4, 8, 15, 15, 15].into_iter().enumerate() {
            c.wait(100);
            assert_eq!(c.next_retry_at, Some(100 + delay));
            assert_eq!(c.retry_count, i as u32 + 1);
            assert!(c.retry_enabled);
        }
        c.recovered();
        assert_eq!(c.retry_count, 0);
        c.wait(200);
        assert_eq!(c.next_retry_at, Some(201));
    }
    #[test]
    fn disconnect_heartbeat_race_and_target_switch_invalidate_old_callbacks() {
        let mut c = Connection::default();
        let old = c.select("a").unwrap();
        c.set(old, Phase::Connected);
        c.wait(1); // disconnect event wins
        assert!(!c.set(old, Phase::Failed));
        assert!(!c.set(old, Phase::Connected));
        let next = c.select("b").unwrap();
        assert!(!c.current(old));
        assert!(c.current(next));
        c.cancel();
        assert!(!c.current(next));
        assert!(!c.retry_enabled);
        assert_eq!(c.next_retry_at, None);
        c.idle();
        let manual = c.retry().unwrap();
        assert!(c.retry_enabled);
        assert!(c.current(manual));
    }
}
