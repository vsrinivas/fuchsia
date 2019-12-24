// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use thiserror::Error;

/// Coordinates access of all available ICMP sequence numbers for an ICMP connection.
#[derive(Clone)]
pub struct SequenceStore {
    /// Next sequence number available for forming an ICMP echo request.
    next_seq: u16,

    /// Last sequence number received from an ICMP echo reply.
    last_seq: u16,

    /// Instant in which the first payload was generated. Used as an offset for the purpose of not
    /// exposing the system clock over the network. This is important for security.
    start_time: zx::Time,
}

/// All ICMP sequence numbers have been used. Create a new ICMP connection to refresh the pool of
/// sequence numbers.
#[derive(Error, Debug)]
#[error("No sequence numbers available")]
pub struct OutOfSequencesError;

#[derive(Error, Debug)]
pub enum GiveError {
    /// Received an ICMP echo reply with a sequence number that has not been sent out yet. Most
    /// likely indicates a misbehaving host.
    #[error("Sequence number received that has not been sent out yet")]
    DoesNotExist(Option<zx::Duration>),

    /// Received an ICMP echo reply with a sequence number below the expected sequence number.
    /// Most likely indicates a misbehaving host or network malfunction.
    #[error("Duplicate sequence number received")]
    Duplicate(Option<zx::Duration>),

    /// Received an ICMP echo reply with a sequence number above the expected sequence number.
    /// Most likely indicates a network misconfiguration or malfunction.
    #[error("Out-of-order sequence number received")]
    OutOfOrder(Option<zx::Duration>),
}

impl SequenceStore {
    /// Create a new ICMP sequence number store.
    pub fn new() -> Self {
        SequenceStore {
            next_seq: 0,
            last_seq: std::u16::MAX,
            start_time: zx::Time::get(zx::ClockId::Monotonic),
        }
    }

    /// Take a sequence number and timestamp for use in an ICMP echo request.
    pub fn take(&mut self) -> Result<(u16, zx::Duration), OutOfSequencesError> {
        let seq_num = self.next_seq;
        let (next_seq, overflow) = self.next_seq.overflowing_add(1);
        self.next_seq = next_seq;
        if overflow {
            return Err(OutOfSequencesError);
        }
        Ok((seq_num, zx::Time::get(zx::ClockId::Monotonic) - self.start_time))
    }

    /// Give the sequence number of an ICMP echo reply back to the store. Returns the latency and
    /// any possible errors.
    pub fn give(
        &mut self,
        sequence_num: u16,
        timestamp: Option<zx::Duration>,
    ) -> Result<Option<zx::Duration>, GiveError> {
        let now = zx::Time::get(zx::ClockId::Monotonic);
        let duration = timestamp.map(|time| now - self.start_time - time);

        if sequence_num >= self.next_seq {
            return Err(GiveError::DoesNotExist(duration));
        }

        let (expected, _) = self.last_seq.overflowing_add(1);
        if sequence_num == expected {
            self.last_seq = sequence_num;
            Ok(duration)
        } else if sequence_num < expected {
            Err(GiveError::Duplicate(duration))
        } else {
            Err(GiveError::OutOfOrder(duration))
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn take_all() {
        let mut s = SequenceStore::new();
        for i in 0..std::u16::MAX {
            assert_eq!(s.take().expect("Failed to take").0, i);
        }
    }

    #[test]
    fn take_too_much() {
        let mut s = SequenceStore::new();
        for i in 0..std::u16::MAX {
            assert_eq!(s.take().expect("Failed to take").0, i);
        }
        s.take().expect_err("There shouldn't be any more sequence numbers available");
    }

    #[test]
    fn give_duration() {
        let mut s = SequenceStore::new();
        let (num, time) = s.take().expect("Failed to take");
        assert_ne!(time.into_nanos(), 0);
        assert!(s
            .give(num, Some(time + zx::Duration::from_nanos(1)))
            .expect("Failed to give")
            .is_some());
    }

    #[test]
    fn give_out_of_order() {
        let mut s = SequenceStore::new();
        s.take().expect("Failed to take");
        let (n, _) = s.take().expect("Failed to take");
        s.give(n, None).expect_err("Should error on out-of-order responses");
    }

    #[test]
    fn give_duplicate() {
        let mut s = SequenceStore::new();
        s.give(42, None).expect_err("Should not be able to give seq_nums that haven't been given");
    }

    #[test]
    fn give_same_number() {
        let mut s = SequenceStore::new();
        let (num, _) = s.take().expect("Failed to take");
        s.give(num, None).expect("Failed to give");
        s.give(num, None).expect_err("Should not be able to give duplicates");
    }

    #[test]
    fn different_gives_returns_different_instants() {
        let mut s = SequenceStore::new();
        let (a, a_time) = s.take().expect("Failed to take");
        let (b, b_time) = s.take().expect("Failed to take");
        assert_ne!(a_time, b_time);

        let a_time = s.give(a, Some(a_time + zx::Duration::from_nanos(1))).expect("Failed to give");
        let b_time = s.give(b, Some(b_time + zx::Duration::from_nanos(1))).expect("Failed to give");
        assert_ne!(a_time, b_time);
    }
}
