// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
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
            start_time: fasync::Time::now().into_zx(),
        }
    }

    /// Take a sequence number and time offset for use in an ICMP echo request.
    pub fn take(&mut self) -> Result<(u16, zx::Duration), OutOfSequencesError> {
        let seq_num = self.next_seq;
        let (next_seq, overflow) = self.next_seq.overflowing_add(1);
        self.next_seq = next_seq;
        if overflow {
            return Err(OutOfSequencesError);
        }
        let offset_from_start_time = fasync::Time::now().into_zx() - self.start_time;
        Ok((seq_num, offset_from_start_time))
    }

    /// Give the sequence number of an ICMP echo reply back to the store. Returns the latency and
    /// any possible errors.
    pub fn give(
        &mut self,
        sequence_num: u16,
        offset_from_start_time: Option<zx::Duration>,
    ) -> Result<Option<zx::Duration>, GiveError> {
        let now = fasync::Time::now().into_zx();
        let latency = offset_from_start_time.map(|time| now - self.start_time - time);

        if sequence_num >= self.next_seq {
            return Err(GiveError::DoesNotExist(latency));
        }

        let (expected, _) = self.last_seq.overflowing_add(1);
        if sequence_num == expected {
            self.last_seq = sequence_num;
            Ok(latency)
        } else if sequence_num < expected {
            Err(GiveError::Duplicate(latency))
        } else {
            Err(GiveError::OutOfOrder(latency))
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    const ZERO_DURATION: zx::Duration = zx::Duration::from_nanos(0);
    const SHORT_DELAY: zx::Duration = zx::Duration::from_nanos(1);

    #[test]
    fn take_all() {
        let _executor =
            fasync::TestExecutor::new_with_fake_time().expect("Failed to create executor");
        let mut s = SequenceStore::new();
        for i in 0..std::u16::MAX {
            let (seq, offset) = s.take().expect("Failed to take");
            assert_eq!(seq, i);
            assert_eq!(offset, ZERO_DURATION);
        }
    }

    #[test]
    fn take_too_much() {
        let _executor =
            fasync::TestExecutor::new_with_fake_time().expect("Failed to create executor");
        let mut s = SequenceStore::new();
        for i in 0..std::u16::MAX {
            let (seq, offset) = s.take().expect("Failed to take");
            assert_eq!(seq, i);
            assert_eq!(offset, ZERO_DURATION);
        }
        matches::assert_matches!(s.take(), Err(OutOfSequencesError {}));
    }

    #[test]
    fn give_out_of_order() {
        let _executor =
            fasync::TestExecutor::new_with_fake_time().expect("Failed to create executor");
        let mut s = SequenceStore::new();
        let (seq, offset) = s.take().expect("Failed to take");
        assert_eq!(seq, 0);
        assert_eq!(offset, ZERO_DURATION);
        let (seq, offset) = s.take().expect("Failed to take");
        assert_eq!(seq, 1);
        assert_eq!(offset, ZERO_DURATION);
        matches::assert_matches!(s.give(seq, None), Err(GiveError::OutOfOrder(None)));
    }

    #[test]
    fn give_duplicate() {
        let _executor =
            fasync::TestExecutor::new_with_fake_time().expect("Failed to create executor");
        let mut s = SequenceStore::new();
        matches::assert_matches!(s.give(42, None), Err(GiveError::DoesNotExist(None)));
    }

    #[test]
    fn give_same_number() {
        let _executor =
            fasync::TestExecutor::new_with_fake_time().expect("Failed to create executor");
        let mut s = SequenceStore::new();
        let (seq, offset) = s.take().expect("Failed to take");
        assert_eq!(seq, 0);
        assert_eq!(offset, ZERO_DURATION);
        let latency = s.give(seq, None).expect("Failed to give");
        assert_eq!(latency, None);
        matches::assert_matches!(s.give(seq, None), Err(GiveError::Duplicate(None)));
    }

    #[test]
    fn give_same_times() {
        let _executor =
            fasync::TestExecutor::new_with_fake_time().expect("Failed to create executor");
        let mut s = SequenceStore::new();

        let (seq, a_time) = s.take().expect("Failed to take");
        assert_eq!(seq, 0);
        let (seq, b_time) = s.take().expect("Failed to take");
        assert_eq!(seq, 1);
        assert_eq!(a_time, b_time);
    }

    #[test]
    fn give_different_times() {
        let executor =
            fasync::TestExecutor::new_with_fake_time().expect("Failed to create executor");
        let mut s = SequenceStore::new();

        let (seq, a_time) = s.take().expect("Failed to take");
        assert_eq!(seq, 0);
        executor.set_fake_time(executor.now() + SHORT_DELAY);
        let (seq, b_time) = s.take().expect("Failed to take");
        assert_eq!(seq, 1);
        assert_eq!(b_time - a_time, SHORT_DELAY);
    }

    #[test]
    fn give_latency() {
        let executor =
            fasync::TestExecutor::new_with_fake_time().expect("Failed to create executor");
        let mut s = SequenceStore::new();

        let (a, a_time) = s.take().expect("Failed to take");
        executor.set_fake_time(executor.now() + SHORT_DELAY);

        let a_latency = s.give(a, Some(a_time)).expect("Failed to give");
        assert_eq!(a_latency, Some(SHORT_DELAY));
    }

    #[test]
    fn give_same_latencies() {
        let executor =
            fasync::TestExecutor::new_with_fake_time().expect("Failed to create executor");
        let mut s = SequenceStore::new();

        let (a, a_time) = s.take().expect("Failed to take");
        executor.set_fake_time(executor.now() + SHORT_DELAY);
        let (b, b_time) = s.take().expect("Failed to take");

        let a_latency = s.give(a, Some(a_time)).expect("Failed to give");
        executor.set_fake_time(executor.now() + SHORT_DELAY);
        let b_latency = s.give(b, Some(b_time)).expect("Failed to give");
        assert_eq!(a_latency, b_latency);
    }

    #[test]
    fn give_different_latencies() {
        let executor =
            fasync::TestExecutor::new_with_fake_time().expect("Failed to create executor");
        let mut s = SequenceStore::new();

        let (a, a_time) = s.take().expect("Failed to take");
        executor.set_fake_time(executor.now() + SHORT_DELAY);
        let (b, b_time) = s.take().expect("Failed to take");

        let a_latency = s.give(a, Some(a_time)).expect("Failed to give");
        let b_latency = s.give(b, Some(b_time)).expect("Failed to give");
        assert_eq!(a_latency.unwrap() - b_latency.unwrap(), SHORT_DELAY);
    }
}
