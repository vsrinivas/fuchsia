// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client::TimedEvent,
        timer::{EventId, Timer},
    },
    fuchsia_zircon::{self as zx, DurationNum},
    wlan_common::time::TimeUnit,
};

/// Struct used to count remaining time BSS has not been detected. Used to determine
/// when trigger auto deauth.
pub struct LostBssCounter {
    /// Period it takes for BSS to send a single beacon
    beacon_period: zx::Duration,
    /// The number of beacon periods where client doesn't receive a single beacon frame
    /// before it declares BSS as lost
    full_timeout_beacon_count: u32,

    /// The remaining time we'll wait for a beacon before deauthenticating
    remaining_timeout: zx::Duration,
    /// The last time we re-calculated the |remaining_timeout|
    /// Note: after unpaused, this is set to current time to make computation easier
    last_accounted: zx::Time,
    timeout_id: Option<EventId>,
}

impl LostBssCounter {
    pub fn start(
        timer: &mut Timer<TimedEvent>,
        beacon_period: zx::Duration,
        full_timeout_beacon_count: u32,
    ) -> Self {
        let remaining_timeout = beacon_period * full_timeout_beacon_count;
        let last_accounted = timer.now();
        let mut this = Self {
            beacon_period,
            full_timeout_beacon_count,

            remaining_timeout,
            last_accounted,
            timeout_id: None,
        };
        this.schedule_timeout(timer, last_accounted + remaining_timeout);
        this
    }

    pub fn pause(&mut self, timer: &mut Timer<TimedEvent>) {
        if let Some(id) = self.timeout_id.take() {
            timer.cancel_event(id);
            let unaccounted_time = timer.now() - self.last_accounted;
            self.remaining_timeout =
                std::cmp::max(self.remaining_timeout - unaccounted_time, 0.millis());
        }
    }

    pub fn unpause(&mut self, timer: &mut Timer<TimedEvent>) {
        let now = timer.now();
        // Schedule remaining timeout. If there's no remaining timeout, schedule 1 TimeUnit
        // in advance (1 TimeUnit was selected arbitrarily, we just want some small value
        // sufficiently far in the future)
        let deadline = now + std::cmp::max(self.remaining_timeout, TimeUnit(1).into());
        self.schedule_timeout(timer, deadline);
        self.last_accounted = now;
    }

    pub fn reset_timeout(&mut self, timer: &mut Timer<TimedEvent>) {
        self.remaining_timeout = self.beacon_period * self.full_timeout_beacon_count;
        self.last_accounted = timer.now();
    }

    /// Return true if BSS is considered lost.
    #[must_use]
    pub fn handle_timeout(&mut self, timer: &mut Timer<TimedEvent>, timeout_id: EventId) -> bool {
        match self.timeout_id {
            Some(id) if id == timeout_id => (),
            _ => return false,
        }

        let now = timer.now();
        if self.remaining_timeout > now - self.last_accounted {
            self.remaining_timeout -= now - self.last_accounted;
            self.last_accounted = now;
            self.schedule_timeout(timer, now + self.remaining_timeout);
            return false;
        }
        true
    }

    fn schedule_timeout(&mut self, timer: &mut Timer<TimedEvent>, deadline: zx::Time) {
        self.timeout_id.replace(timer.schedule_event(deadline, TimedEvent::LostBssCountdown));
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::timer::FakeScheduler, wlan_common::assert_variant};

    const TEST_TIMEOUT_BCN_COUNT: u32 = 1000;

    #[test]
    fn test_single_uninterrupted_period() {
        let mut fake_scheduler = FakeScheduler::new();
        let mut timer = Timer::new(fake_scheduler.as_scheduler());
        let mut counter = LostBssCounter::start(&mut timer, bcn_period(), TEST_TIMEOUT_BCN_COUNT);

        let (id, deadline) = assert_variant!(fake_scheduler.next_event(), Some(ev) => ev);
        assert!(timer.triggered(&id).is_some());
        assert_eq!(deadline, zx::Time::from_nanos(0) + (bcn_period() * TEST_TIMEOUT_BCN_COUNT));
        // Verify `handle_timeout` returns true, indicating auto-deauth
        fake_scheduler.increment_time(bcn_period() * TEST_TIMEOUT_BCN_COUNT);
        assert!(counter.handle_timeout(&mut timer, id));
    }

    #[test]
    fn test_beacon_received_midway() {
        let mut fake_scheduler = FakeScheduler::new();
        let mut timer = Timer::new(fake_scheduler.as_scheduler());
        let mut counter = LostBssCounter::start(&mut timer, bcn_period(), TEST_TIMEOUT_BCN_COUNT);

        let (id, deadline) = assert_variant!(fake_scheduler.next_event(), Some(ev) => ev);
        assert!(timer.triggered(&id).is_some());
        assert_eq!(deadline, zx::Time::from_nanos(0) + (bcn_period() * TEST_TIMEOUT_BCN_COUNT));

        // Beacon received some time later, resetting the timeout.
        fake_scheduler.increment_time(bcn_period() * (TEST_TIMEOUT_BCN_COUNT - 1));
        counter.reset_timeout(&mut timer);

        // Verify that calling `handle_timeout` at originally scheduled time would not
        // return false, indicating no auto-deauth yet
        fake_scheduler.increment_time(bcn_period());
        assert!(!counter.handle_timeout(&mut timer, id));

        // LostBssCounter should schedule another timeout
        let (id, deadline) = assert_variant!(fake_scheduler.next_event(), Some(ev) => ev);
        assert!(timer.triggered(&id).is_some());
        assert_eq!(
            deadline,
            zx::Time::from_nanos(0) + (bcn_period() * (TEST_TIMEOUT_BCN_COUNT * 2 - 1))
        );

        // Verify `handle_timeout` returns true, indicating auto-deauth
        fake_scheduler.increment_time(bcn_period() * (TEST_TIMEOUT_BCN_COUNT - 1));
        assert!(counter.handle_timeout(&mut timer, id));
    }

    #[test]
    fn test_pause_unpause() {
        let mut fake_scheduler = FakeScheduler::new();
        let mut timer = Timer::new(fake_scheduler.as_scheduler());
        let mut counter = LostBssCounter::start(&mut timer, bcn_period(), TEST_TIMEOUT_BCN_COUNT);

        let (id, _deadline) = assert_variant!(fake_scheduler.next_event(), Some(ev) => ev);
        assert_eq!(timer.scheduled_event_count(), 1);
        counter.pause(&mut timer);
        assert_eq!(timer.scheduled_event_count(), 0);

        // Timeout shouldn't be triggered while paused, but even if it does, it would
        // have no effect
        fake_scheduler.increment_time(bcn_period() * TEST_TIMEOUT_BCN_COUNT * 2);
        assert!(!counter.handle_timeout(&mut timer, id));

        // When unpaused, timeout should be rescheduled again
        counter.unpause(&mut timer);
        let (id, deadline) = assert_variant!(fake_scheduler.next_event(), Some(ev) => ev);
        assert!(timer.triggered(&id).is_some());
        assert_eq!(deadline, zx::Time::from_nanos(0) + (bcn_period() * TEST_TIMEOUT_BCN_COUNT * 3));

        // Verify `handle_timeout` returns true, indicating auto-deauth
        fake_scheduler.increment_time(bcn_period() * TEST_TIMEOUT_BCN_COUNT);
        assert!(counter.handle_timeout(&mut timer, id));
    }

    #[test]
    fn test_pause_right_when_timeout_duration_is_exhausted() {
        let mut fake_scheduler = FakeScheduler::new();
        let mut timer = Timer::new(fake_scheduler.as_scheduler());
        let mut counter = LostBssCounter::start(&mut timer, bcn_period(), TEST_TIMEOUT_BCN_COUNT);

        let (_id, _deadline) = assert_variant!(fake_scheduler.next_event(), Some(ev) => ev);
        fake_scheduler.increment_time(bcn_period() * TEST_TIMEOUT_BCN_COUNT);
        counter.pause(&mut timer);
        assert_eq!(timer.scheduled_event_count(), 0);

        // When unpaused, timeout is scheduled again, but 1 beacon period in the future.
        counter.unpause(&mut timer);
        let (id, deadline) = assert_variant!(fake_scheduler.next_event(), Some(ev) => ev);
        assert!(timer.triggered(&id).is_some());
        assert_eq!(deadline, timer.now() + TimeUnit(1).into());

        // Verify `handle_timeout` returns true, indicating auto-deauth
        fake_scheduler.increment_time(TimeUnit(1).into());
        assert!(counter.handle_timeout(&mut timer, id));
    }

    fn bcn_period() -> zx::Duration {
        TimeUnit::DEFAULT_BEACON_INTERVAL.into()
    }
}
