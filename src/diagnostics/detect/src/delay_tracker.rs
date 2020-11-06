// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// DelayTracker keeps track of how recently we sent a crash report of each type, and whether we
/// should mute too-frequent requests.
use {
    crate::{Mode, MINIMUM_SIGNATURE_INTERVAL_NANOS},
    injectable_time::TimeSource,
    log::warn,
    std::collections::HashMap,
    triage::SnapshotTrigger,
};

pub struct DelayTracker<'a> {
    last_sent: HashMap<String, i64>,
    time_source: &'a dyn TimeSource,
    program_mode: Mode,
}

impl<'a> DelayTracker<'a> {
    pub fn new(time_source: &'a dyn TimeSource, program_mode: &Mode) -> DelayTracker<'a> {
        DelayTracker { last_sent: HashMap::new(), time_source, program_mode: *program_mode }
    }

    fn appropriate_report_interval(&self, desired_interval: i64) -> i64 {
        if self.program_mode == Mode::Test || desired_interval >= MINIMUM_SIGNATURE_INTERVAL_NANOS {
            desired_interval
        } else {
            MINIMUM_SIGNATURE_INTERVAL_NANOS
        }
    }

    // If it's OK to send, remember the time and return true.
    pub fn ok_to_send(&mut self, snapshot: &SnapshotTrigger) -> bool {
        let now = self.time_source.now();
        let interval = self.appropriate_report_interval(snapshot.interval);
        let should_send = match self.last_sent.get(&snapshot.signature) {
            None => true,
            Some(time) => time <= &(now - interval),
        };
        if should_send {
            self.last_sent.insert(snapshot.signature.to_string(), now);
            if snapshot.interval < MINIMUM_SIGNATURE_INTERVAL_NANOS {
                // To reduce logspam, put this warning here rather than above where the
                // calculation is. The calculation may happen every time we check diagnostics; this
                // will happen at most every MINIMUM_SIGNATURE_INTERVAL (except in tests).
                warn!(
                    "Signature {} has interval {} nanos, less than minimum {}",
                    snapshot.signature, snapshot.interval, MINIMUM_SIGNATURE_INTERVAL_NANOS
                );
            }
        }
        should_send
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use injectable_time::FakeTime;

    #[test]
    fn verify_test_mode() {
        let time = FakeTime::new();
        let mut tracker = DelayTracker::new(&time, &Mode::Test);
        time.set(1);
        let trigger_slow = SnapshotTrigger { signature: "slow".to_string(), interval: 10 };
        let trigger_fast = SnapshotTrigger { signature: "fast".to_string(), interval: 1 };
        let ok_slow_1 = tracker.ok_to_send(&trigger_slow);
        let ok_fast_1 = tracker.ok_to_send(&trigger_fast);
        time.set(3);
        let ok_slow_2 = tracker.ok_to_send(&trigger_slow);
        let ok_fast_2 = tracker.ok_to_send(&trigger_fast);
        // This one should obviously succeed.
        assert_eq!(ok_slow_1, true);
        // It should allow a different snapshot signature too.
        assert_eq!(ok_fast_1, true);
        // It should reject the first (slow) signature the second time.
        assert_eq!(ok_slow_2, false);
        // The second (fast) signature should be accepted repeatedly.
        assert_eq!(ok_fast_2, true);
    }

    #[test]
    fn verify_appropriate_report_interval() {
        assert!(MINIMUM_SIGNATURE_INTERVAL_NANOS > 1);
        let time = FakeTime::new();
        let test_tracker = DelayTracker::new(&time, &Mode::Test);
        let production_tracker = DelayTracker::new(&time, &Mode::Production);

        assert_eq!(test_tracker.appropriate_report_interval(1), 1);
        assert_eq!(
            production_tracker.appropriate_report_interval(1),
            MINIMUM_SIGNATURE_INTERVAL_NANOS
        );
    }
}
