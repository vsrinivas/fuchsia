// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client::network_selection::NetworkSelector, config_management::SavedNetworksManagerApi,
        telemetry::TelemetrySender, util::listener,
    },
    anyhow::Error,
    fuchsia_async as fasync,
    futures::{channel::mpsc, lock::Mutex, Future},
    std::sync::Arc,
    void::Void,
};

mod iface_manager;
pub mod iface_manager_api;
mod iface_manager_types;
pub mod low_power_manager;
pub mod phy_manager;

pub fn create_iface_manager(
    phy_manager: Arc<Mutex<dyn phy_manager::PhyManagerApi + Send>>,
    client_update_sender: listener::ClientListenerMessageSender,
    ap_update_sender: listener::ApListenerMessageSender,
    dev_monitor_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
    saved_networks: Arc<dyn SavedNetworksManagerApi>,
    network_selector: Arc<NetworkSelector>,
    telemetry_sender: TelemetrySender,
) -> (Arc<Mutex<iface_manager_api::IfaceManager>>, impl Future<Output = Result<Void, Error>>) {
    let (sender, receiver) = mpsc::channel(0);
    let iface_manager_sender = Arc::new(Mutex::new(iface_manager_api::IfaceManager { sender }));
    let (stats_sender, stats_receiver) = mpsc::unbounded();
    let (defect_sender, defect_receiver) = mpsc::unbounded();
    let iface_manager = iface_manager::IfaceManagerService::new(
        phy_manager,
        client_update_sender,
        ap_update_sender,
        dev_monitor_proxy,
        saved_networks,
        telemetry_sender,
        stats_sender,
        defect_sender,
    );
    let iface_manager_service = iface_manager::serve_iface_manager_requests(
        iface_manager,
        iface_manager_sender.clone(),
        network_selector,
        receiver,
        stats_receiver,
        defect_receiver,
    );

    (iface_manager_sender, iface_manager_service)
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum PhyFailure {
    IfaceCreationFailure { phy_id: u16 },
    IfaceDestructionFailure { phy_id: u16 },
}

#[derive(Clone, Copy, Debug)]
pub enum IfaceFailure {
    CanceledScan { iface_id: u16 },
    FailedScan { iface_id: u16 },
    EmptyScanResults { iface_id: u16 },
    ApStartFailure { iface_id: u16 },
}

// Interfaces will come and go and each one will receive a different ID.  The failures are
// ultimately all associated with a given PHY and we will be interested in tallying up how many
// of a given failure type a PHY has seen when making recovery decisions.  As such, only the
// IfaceFailure variant should be considered when determining equality.  The contained interface ID
// is useful only for associating a failure with a PHY.
impl PartialEq for IfaceFailure {
    fn eq(&self, other: &Self) -> bool {
        match (*self, *other) {
            (IfaceFailure::CanceledScan { .. }, IfaceFailure::CanceledScan { .. }) => true,
            (IfaceFailure::FailedScan { .. }, IfaceFailure::FailedScan { .. }) => true,
            (IfaceFailure::EmptyScanResults { .. }, IfaceFailure::EmptyScanResults { .. }) => true,
            (IfaceFailure::ApStartFailure { .. }, IfaceFailure::ApStartFailure { .. }) => true,
            _ => false,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Defect {
    Phy(PhyFailure),
    Iface(IfaceFailure),
}

#[derive(Debug, PartialEq)]
struct Event<T: PartialEq> {
    value: T,
    time: fasync::Time,
}

impl<T: PartialEq> Event<T> {
    fn new(value: T, time: fasync::Time) -> Self {
        Event { value, time }
    }
}

pub struct EventHistory<T: PartialEq> {
    events: Vec<Event<T>>,
    retention_time: fuchsia_zircon::Duration,
}

impl<T: PartialEq> EventHistory<T> {
    fn new(retention_seconds: u32) -> Self {
        EventHistory {
            events: Vec::new(),
            retention_time: fuchsia_zircon::Duration::from_seconds(retention_seconds as i64),
        }
    }

    fn add_event(&mut self, value: T) {
        let curr_time = fasync::Time::now();
        self.events.push(Event::new(value, curr_time));
        self.retain_unexpired_events(curr_time);
    }

    #[cfg(test)]
    fn event_count(&mut self, value: T) -> usize {
        let curr_time = fasync::Time::now();
        self.retain_unexpired_events(curr_time);
        self.events.iter().filter(|event| event.value == value).count()
    }

    #[cfg(test)]
    fn time_since_last_event(&mut self, value: T) -> Option<fuchsia_zircon::Duration> {
        let curr_time = fasync::Time::now();
        self.retain_unexpired_events(curr_time);

        for event in self.events.iter().rev() {
            if event.value == value {
                return Some(curr_time - event.time);
            }
        }
        None
    }

    fn retain_unexpired_events(&mut self, curr_time: fasync::Time) {
        let oldest_allowed_time = curr_time - self.retention_time;
        self.events.retain(|event| event.time > oldest_allowed_time)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async::TestExecutor,
        rand::Rng,
        test_util::{assert_gt, assert_lt},
    };

    #[fuchsia::test]
    fn test_event_retention() {
        // Allow for events to be retained for at most 1s.
        let mut event_history = EventHistory::<()>::new(1);

        // Add events at 0, 1, 2, 2 and a little bit seconds, and 3s.
        event_history.events = vec![
            Event::<()> { value: (), time: fasync::Time::from_nanos(0) },
            Event::<()> { value: (), time: fasync::Time::from_nanos(1_000_000_000) },
            Event::<()> { value: (), time: fasync::Time::from_nanos(2_000_000_000) },
            Event::<()> { value: (), time: fasync::Time::from_nanos(2_000_000_001) },
            Event::<()> { value: (), time: fasync::Time::from_nanos(3_000_000_000) },
        ];

        // Retain those events within the retention window based on a current time of 3s.
        event_history.retain_unexpired_events(fasync::Time::from_nanos(3_000_000_000));

        // It is expected that the events at 2 and a little bit seconds and 3s are retained while
        // the others are discarded.
        assert_eq!(
            event_history.events,
            vec![
                Event::<()> { value: (), time: fasync::Time::from_nanos(2_000_000_001) },
                Event::<()> { value: (), time: fasync::Time::from_nanos(3_000_000_000) },
            ]
        );
    }

    #[derive(Debug, PartialEq)]
    enum TestEnum {
        Foo,
        Bar,
    }

    #[fuchsia::test]
    fn test_time_since_last_event() {
        // An executor is required to enable querying time.
        let _exec = TestExecutor::new().expect("failed to create an executor");

        // Allow events to be stored basically forever.  The goal here is to ensure that the
        // retention policy does not discard any of our events.
        let mut event_history = EventHistory::<TestEnum>::new(u32::MAX);

        // Add some events with known timestamps.
        let foo_time: i64 = 1_123_123_123;
        let bar_time: i64 = 2_222_222_222;
        event_history.events = vec![
            Event { value: TestEnum::Foo, time: fasync::Time::from_nanos(foo_time) },
            Event { value: TestEnum::Bar, time: fasync::Time::from_nanos(bar_time) },
        ];

        // Get the time before and after the function calls were made.  This allows for some slack
        // in evaluating whether the time calculations are in the realm of accurate.
        let start_time = fasync::Time::now().into_nanos();
        let time_since_foo =
            event_history.time_since_last_event(TestEnum::Foo).expect("Foo was not retained");
        let time_since_bar =
            event_history.time_since_last_event(TestEnum::Bar).expect("Bar was not retained");
        let end_time = fasync::Time::now().into_nanos();

        // Make sure the returned durations are within bounds.
        assert_lt!(time_since_foo.into_nanos(), end_time - foo_time);
        assert_gt!(time_since_foo.into_nanos(), start_time - foo_time);

        assert_lt!(time_since_bar.into_nanos(), end_time - bar_time);
        assert_gt!(time_since_bar.into_nanos(), start_time - bar_time);
    }

    #[fuchsia::test]
    fn test_time_since_last_event_retention() {
        // An executor is required to enable querying time.
        let _exec = TestExecutor::new().expect("failed to create an executor");

        // Set the retention time to slightly less than the current time.  This number will be
        // positive.  Since it will occupy the positive range of i64, it is safe to cast it as u32.
        let curr_time_seconds = fasync::Time::now().into_nanos() / 1_000_000_000;
        let mut event_history = EventHistory::<()>::new((curr_time_seconds - 1) as u32);

        // Put in an event at time zero so that it will not be retained when querying recent
        // events.
        event_history.events.push(Event::<()> { value: (), time: fasync::Time::from_nanos(0) });

        assert_eq!(event_history.time_since_last_event(()), None);
    }
    #[fuchsia::test]
    fn test_add_event() {
        // An executor is required to enable querying time.
        let _exec = TestExecutor::new().expect("failed to create an executor");
        let mut event_history = EventHistory::<()>::new(u32::MAX);

        // Add a few events
        let num_events = 3;
        let start_time = fasync::Time::now().into_nanos();
        for _ in 0..num_events {
            event_history.add_event(());
        }
        let end_time = fasync::Time::now().into_nanos();

        // All three of the recent events should have been retained.
        assert_eq!(event_history.events.len(), num_events);

        // Verify that all of the even timestamps are within range.
        for event in event_history.events {
            let event_time = event.time.into_nanos();
            assert_lt!(event_time, end_time);
            assert_gt!(event_time, start_time);
        }
    }

    #[fuchsia::test]
    fn test_add_event_retention() {
        // An executor is required to enable querying time.
        let _exec = TestExecutor::new().expect("failed to create an executor");

        // Set the retention time to slightly less than the current time.  This number will be
        // positive.  Since it will occupy the positive range of i64, it is safe to cast it as u32.
        let curr_time_seconds = fasync::Time::now().into_nanos() / 1_000_000_000;
        let mut event_history = EventHistory::<()>::new((curr_time_seconds - 1) as u32);

        // Put in an event at time zero so that it will not be retained when querying recent
        // events.
        event_history.events.push(Event::<()> { value: (), time: fasync::Time::from_nanos(0) });

        // Add an event and observe that the event from time 0 has been removed.
        let start_time = fasync::Time::now().into_nanos();
        event_history.add_event(());
        assert_eq!(event_history.events.len(), 1);

        // Add a couple more events.
        event_history.add_event(());
        event_history.add_event(());
        let end_time = fasync::Time::now().into_nanos();

        // All three of the recent events should have been retained.
        assert_eq!(event_history.events.len(), 3);

        // Verify that all of the even timestamps are within range.
        for event in event_history.events {
            let event_time = event.time.into_nanos();
            assert_lt!(event_time, end_time);
            assert_gt!(event_time, start_time);
        }
    }

    #[fuchsia::test]
    fn test_event_count() {
        // An executor is required to enable querying time.
        let _exec = TestExecutor::new().expect("failed to create an executor");
        let mut event_history = EventHistory::<TestEnum>::new(u32::MAX);

        event_history.events = vec![
            Event { value: TestEnum::Foo, time: fasync::Time::from_nanos(0) },
            Event { value: TestEnum::Foo, time: fasync::Time::from_nanos(1) },
            Event { value: TestEnum::Bar, time: fasync::Time::from_nanos(2) },
            Event { value: TestEnum::Bar, time: fasync::Time::from_nanos(3) },
            Event { value: TestEnum::Foo, time: fasync::Time::from_nanos(4) },
        ];

        assert_eq!(event_history.event_count(TestEnum::Foo), 3);
        assert_eq!(event_history.event_count(TestEnum::Bar), 2);
    }

    #[fuchsia::test]
    fn test_event_count_retention() {
        // An executor is required to enable querying time.
        let _exec = TestExecutor::new().expect("failed to create an executor");

        // Set the retention time to slightly less than the current time.  This number will be
        // positive.  Since it will occupy the positive range of i64, it is safe to cast it as u32.
        let curr_time_seconds = fasync::Time::now().into_nanos() / 1_000_000_000;
        let mut event_history = EventHistory::<TestEnum>::new((curr_time_seconds - 1) as u32);

        event_history.events = vec![
            Event { value: TestEnum::Foo, time: fasync::Time::from_nanos(0) },
            Event { value: TestEnum::Foo, time: fasync::Time::from_nanos(0) },
            Event { value: TestEnum::Bar, time: fasync::Time::now() },
            Event { value: TestEnum::Bar, time: fasync::Time::now() },
            Event { value: TestEnum::Foo, time: fasync::Time::now() },
        ];

        assert_eq!(event_history.event_count(TestEnum::Foo), 1);
        assert_eq!(event_history.event_count(TestEnum::Bar), 2);
    }

    #[fuchsia::test]
    fn test_failure_equality() {
        let mut rng = rand::thread_rng();
        assert_eq!(
            IfaceFailure::CanceledScan { iface_id: rng.gen::<u16>() },
            IfaceFailure::CanceledScan { iface_id: rng.gen::<u16>() }
        );
        assert_eq!(
            IfaceFailure::FailedScan { iface_id: rng.gen::<u16>() },
            IfaceFailure::FailedScan { iface_id: rng.gen::<u16>() }
        );
        assert_eq!(
            IfaceFailure::EmptyScanResults { iface_id: rng.gen::<u16>() },
            IfaceFailure::EmptyScanResults { iface_id: rng.gen::<u16>() }
        );
        assert_eq!(
            IfaceFailure::ApStartFailure { iface_id: rng.gen::<u16>() },
            IfaceFailure::ApStartFailure { iface_id: rng.gen::<u16>() }
        );
    }
}
