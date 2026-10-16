use anyhow::{bail, Result};
pub const HEADER: usize = 16;
pub const MAX: usize = 2048;
pub const HELLO: u8 = 1;
// IDs 2-4 are retired session messages.
pub const MIC: u8 = 5;
pub const MIC_ACK: u8 = 6;
pub const AUDIO: u8 = 7;
pub const HEARTBEAT: u8 = 8;
pub const ERROR: u8 = 9;
pub const CAPABILITIES: u8 = 10;
pub const TIME: u8 = 11;
pub const WEATHER: u8 = 12;
pub const SETTINGS_UPDATE: u8 = 13;
pub const DEVICE_STATUS: u8 = 14;
pub const TIMER_STATUS: u8 = 15;
pub const SETTINGS_ACK: u8 = 16;
pub const STOP: u8 = 17;
pub const STOP_ACK: u8 = 18;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Message {
    pub kind: u8,
    pub request: u32,
    pub stream: u32,
    pub sequence: u16,
    pub payload: Vec<u8>,
}
impl Message {
    pub fn new(kind: u8) -> Self {
        Self {
            kind,
            request: 0,
            stream: 0,
            sequence: 0,
            payload: vec![],
        }
    }
    pub fn encode(&self) -> Result<Vec<u8>> {
        if self.payload.len() > MAX {
            bail!("Frame exceeds protocol limit");
        }
        let mut out = vec![0xa5, 0x5a, 1, self.kind];
        out.extend_from_slice(&(self.payload.len() as u16).to_be_bytes());
        out.extend_from_slice(&self.request.to_be_bytes());
        out.extend_from_slice(&self.stream.to_be_bytes());
        out.extend_from_slice(&self.sequence.to_be_bytes());
        out.extend_from_slice(&self.payload);
        Ok(out)
    }
}
#[derive(Default)]
pub struct Parser {
    bytes: Vec<u8>,
}
impl Parser {
    pub fn feed(&mut self, input: &[u8]) -> Vec<Message> {
        let mut messages = vec![];
        for &byte in input {
            if self.bytes.len() == HEADER + MAX {
                self.bytes.remove(0);
            }
            self.bytes.push(byte);
            loop {
                if self.bytes.len() < 2 {
                    break;
                }
                if self.bytes[..2] != [0xa5, 0x5a] {
                    self.bytes.remove(0);
                    continue;
                }
                if self.bytes.len() < HEADER {
                    break;
                }
                let n = u16::from_be_bytes([self.bytes[4], self.bytes[5]]) as usize;
                if self.bytes[2] != 1 || n > MAX {
                    self.bytes.remove(0);
                    continue;
                }
                if self.bytes.len() < HEADER + n {
                    break;
                }
                messages.push(Message {
                    kind: self.bytes[3],
                    request: u32::from_be_bytes(self.bytes[6..10].try_into().unwrap()),
                    stream: u32::from_be_bytes(self.bytes[10..14].try_into().unwrap()),
                    sequence: u16::from_be_bytes(self.bytes[14..16].try_into().unwrap()),
                    payload: self.bytes[HEADER..HEADER + n].to_vec(),
                });
                self.bytes.drain(..HEADER + n);
            }
        }
        messages
    }
}

#[derive(Default)]
pub struct Sequence {
    next: Option<u16>,
}
impl Sequence {
    pub fn accept(&mut self, seq: u16) -> Option<usize> {
        let missing = self.next.map(|n| seq.wrapping_sub(n)).unwrap_or(0);
        if missing >= 0x8000 {
            return None;
        }
        self.next = Some(seq.wrapping_add(1));
        Some(missing as usize)
    }
    pub fn reset(&mut self) {
        self.next = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn shared_vectors_and_every_split() {
        for line in include_str!("../../tests/wire-vectors.txt").lines() {
            if line.starts_with('#') || line.is_empty() {
                continue;
            }
            let bytes: Vec<u8> = line
                .split_whitespace()
                .map(|n| u8::from_str_radix(n, 16).unwrap())
                .collect();
            for split in 0..=bytes.len() {
                let mut p = Parser::default();
                let mut frames = p.feed(&bytes[..split]);
                frames.extend(p.feed(&bytes[split..]));
                assert_eq!(frames.len(), 1);
                assert_eq!(frames[0].encode().unwrap(), bytes);
            }
        }
    }
    #[test]
    fn malformed_resync_and_coalescing() {
        let good = Message::new(HELLO).encode().unwrap();
        let mut bytes = vec![0, 0xa5, 0, 0xa5, 0x5a, 1, 1, 0xff, 0xff];
        bytes.extend([0; 8]);
        bytes.extend(&good);
        bytes.extend(&good);
        assert_eq!(Parser::default().feed(&bytes).len(), 2);
    }
    #[test]
    fn bound_and_sequence_wrap() {
        let mut p = Parser::default();
        p.feed(&vec![0; MAX * 8]);
        assert!(p.bytes.len() <= HEADER + MAX);
        let mut s = Sequence::default();
        assert_eq!(s.accept(65535), Some(0));
        assert_eq!(s.accept(0), Some(0));
        assert_eq!(s.accept(0), None);
        assert_eq!(s.accept(3), Some(2));
    }
}
