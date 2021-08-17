// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_zircon::{self as zx, DurationNum},
    std::collections::VecDeque,
    wlan_sme::client::info::DisconnectInfo,
};

#[derive(Debug)]
pub struct DisconnectTracker {
    events: VecDeque<(zx::Time, DisconnectInfo)>,
}

impl DisconnectTracker {
    pub fn new() -> Self {
        Self { events: VecDeque::new() }
    }

    pub fn add_event(&mut self, info: DisconnectInfo) {
        self.add_event_helper(zx::Time::get_monotonic(), info);
    }

    fn add_event_helper(&mut self, now: zx::Time, info: DisconnectInfo) {
        // Place a hard limit of 100 disconnects to prevent OOM
        while self.events.len() >= 100 {
            self.events.pop_front();
        }
        self.events.push_back((now, info));
    }

    pub fn disconnects_today(&self) -> Vec<DisconnectInfo> {
        self.disconnects_today_helper(zx::Time::get_monotonic())
    }

    fn disconnects_today_helper(&self, now: zx::Time) -> Vec<DisconnectInfo> {
        self.events.iter().filter(|ev| ev.0 > now - 24.hours()).map(|ev| ev.1.clone()).collect()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::telemetry::test_helper::fake_disconnect_info};

    #[test]
    fn test_disconnect_tracker() {
        let now = zx::Time::from_nanos(48.hours().into_nanos());
        let mut disconnect_tracker = DisconnectTracker::new();
        disconnect_tracker.add_event_helper(now - 25.hours(), fake_disconnect_info([1u8; 6]));
        disconnect_tracker.add_event_helper(now - 23.hours(), fake_disconnect_info([2u8; 6]));
        disconnect_tracker.add_event_helper(now - 10.hours(), fake_disconnect_info([3u8; 6]));

        assert_eq!(
            &disconnect_tracker.disconnects_today_helper(now)[..],
            &[fake_disconnect_info([2u8; 6]), fake_disconnect_info([3u8; 6]),]
        );
    }

    #[test]
    fn test_disconnect_tracker_only_keep_latest_100_events() {
        const MAX_TRACKED: usize = 100;

        let now = zx::Time::from_nanos(48.hours().into_nanos());
        let mut disconnect_tracker = DisconnectTracker::new();
        for i in 0..MAX_TRACKED + 1 {
            disconnect_tracker.add_event_helper(now, fake_disconnect_info([i as u8; 6]));
        }

        let tracked_disconnects = disconnect_tracker.disconnects_today_helper(now);
        assert_eq!(tracked_disconnects.len(), MAX_TRACKED);
        for i in 0..MAX_TRACKED {
            assert_eq!(tracked_disconnects[i].bssid.0, [i as u8 + 1u8; 6]);
        }
    }
}
