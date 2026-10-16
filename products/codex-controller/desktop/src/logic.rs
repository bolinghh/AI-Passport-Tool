use crate::wire::Message;
use std::collections::VecDeque;
#[derive(Default)]
pub struct Ledger {
    highest: u32,
    replies: VecDeque<(Message, Message)>,
}
impl Ledger {
    pub fn cached(&self, request: &Message) -> Result<Option<Message>, ()> {
        if request.request == 0 {
            return Err(());
        }
        if let Some((old, reply)) = self
            .replies
            .iter()
            .find(|(r, _)| r.request == request.request)
        {
            return if old == request {
                Ok(Some(reply.clone()))
            } else {
                Err(())
            };
        }
        if request.request <= self.highest {
            return Err(());
        }
        Ok(None)
    }
    pub fn remember(&mut self, request: Message, reply: Message) {
        self.highest = request.request;
        self.replies.push_back((request, reply));
        if self.replies.len() > 8 {
            self.replies.pop_front();
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn duplicate_conflict_and_old_request() {
        let mut l = Ledger::default();
        let mut req = Message::new(5);
        req.request = 1;
        let reply = Message::new(6);
        l.remember(req.clone(), reply.clone());
        assert_eq!(l.cached(&req), Ok(Some(reply)));
        req.stream = 2;
        assert!(l.cached(&req).is_err());
        for n in 2..20 {
            let mut r = Message::new(5);
            r.request = n;
            l.remember(r, Message::new(6));
        }
        req.stream = 0;
        assert!(l.cached(&req).is_err());
        assert_eq!(l.replies.len(), 8);
    }
}
