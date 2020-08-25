// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType},
    time_metrics_registry::{
        RealTimeClockEventsMetricDimensionEventType as RtcEventType,
        TimekeeperLifecycleEventsMetricDimensionEventType as LifecycleEventType,
        REAL_TIME_CLOCK_EVENTS_METRIC_ID, TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID,
    },
};

/// A collection of convenience functions for logging time metrics to Cobalt.
pub trait CobaltDiagnostics {
    /// Records a Timekeeper lifecycle event.
    fn log_lifecycle_event(&mut self, event_type: LifecycleEventType);
    /// Records an RTC interaction event.
    fn log_rtc_event(&mut self, event_type: RtcEventType);
}

/// A connection to the real Cobalt service.
pub struct CobaltDiagnosticsImpl {
    /// The wrapped CobaltSender used to log metrics.
    sender: CobaltSender,
    // TODO(57677): Move back to an owned fasync::Task instead of detaching the spawned Task
    // once the lifecycle of timekeeper ensures CobaltDiagnostics objects will last long enough
    // to finish their logging.
}

impl CobaltDiagnosticsImpl {
    /// Contructs a new CobaltDiagnostics instance.
    pub fn new() -> Self {
        let (sender, fut) = CobaltConnector::default()
            .serve(ConnectionType::project_id(time_metrics_registry::PROJECT_ID));
        fasync::Task::spawn(fut).detach();
        Self { sender }
    }
}

impl CobaltDiagnostics for CobaltDiagnosticsImpl {
    fn log_lifecycle_event(&mut self, event_type: LifecycleEventType) {
        self.sender.log_event(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID, event_type);
    }

    fn log_rtc_event(&mut self, event_type: RtcEventType) {
        self.sender.log_event(REAL_TIME_CLOCK_EVENTS_METRIC_ID, event_type);
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl_fuchsia_cobalt::{CobaltEvent, Event, EventPayload},
        futures::StreamExt,
    };

    #[fasync::run_until_stalled(test)]
    async fn log_events() {
        let (mpsc_sender, mut mpsc_receiver) = futures::channel::mpsc::channel(1);
        let sender = CobaltSender::new(mpsc_sender);
        let mut diagnostics = CobaltDiagnosticsImpl { sender };

        diagnostics.log_lifecycle_event(LifecycleEventType::InitializedBeforeUtcStart);
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID,
                event_codes: vec![LifecycleEventType::InitializedBeforeUtcStart as u32],
                component: None,
                payload: EventPayload::Event(Event),
            })
        );

        diagnostics.log_rtc_event(RtcEventType::ReadFailed);
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::REAL_TIME_CLOCK_EVENTS_METRIC_ID,
                event_codes: vec![RtcEventType::ReadFailed as u32],
                component: None,
                payload: EventPayload::Event(Event),
            })
        );
    }
}

/// Defines a fake implementation of `CobaltDiagnostics` and a monitor to verify interactions.
#[cfg(test)]
pub mod fake {
    use {
        super::*,
        std::sync::{Arc, Mutex},
    };

    /// The data shared between a `FakeCobaltDiagnostics` and the associated `FakeCobaltMonitor`.
    struct Data {
        /// An ordered list of the lifecycle events received since the last reset.
        lifecycle_events: Vec<LifecycleEventType>,
        /// An ordered list of the RTC events received since the last reset.
        rtc_events: Vec<RtcEventType>,
    }

    /// A fake implementation of `CobaltDiagnostics` that allows for easy unit testing via an
    /// associated `FakeCobaltMonitor`.
    ///
    /// Note: This uses std::sync::Mutex to synchronize access between the diagnostics and monitor
    /// from synchronous methods. If used in combination with asynchronous code on a multithreaded
    /// executor this could potentially deadlock. If this becomes a problems we could switch to an
    /// mpsc communication model similar to the real implementation.
    pub struct FakeCobaltDiagnostics {
        data: Arc<Mutex<Data>>,
    }

    impl FakeCobaltDiagnostics {
        /// Constructs a new `FakeCobaltDiagnostics`/`FakeCobaltMonitor` pair.
        pub fn new() -> (Self, FakeCobaltMonitor) {
            let data = Arc::new(Mutex::new(Data { lifecycle_events: vec![], rtc_events: vec![] }));
            (FakeCobaltDiagnostics { data: Arc::clone(&data) }, FakeCobaltMonitor { data })
        }
    }

    impl CobaltDiagnostics for FakeCobaltDiagnostics {
        fn log_lifecycle_event(&mut self, event_type: LifecycleEventType) {
            let mut data = self.data.lock().expect("Error aquiring mutex");
            data.lifecycle_events.push(event_type);
        }

        fn log_rtc_event(&mut self, event_type: RtcEventType) {
            let mut data = self.data.lock().expect("Error aquiring mutex");
            data.rtc_events.push(event_type);
        }
    }

    /// A monitor to verify interactions with an associated `FakeCobaltDiagnostics`.
    pub struct FakeCobaltMonitor {
        data: Arc<Mutex<Data>>,
    }

    impl FakeCobaltMonitor {
        /// Clears all recorded interactions.
        pub fn reset(&mut self) {
            let mut data = self.data.lock().expect("Error aquiring mutex");
            data.lifecycle_events.clear();
            data.rtc_events.clear();
        }

        /// Panics if the supplied slice does not match the received lifecycle events.
        pub fn assert_lifecycle_events(&self, expected: &[LifecycleEventType]) {
            assert_eq!(self.data.lock().expect("Error aquiring mutex").lifecycle_events, expected);
        }

        /// Panics if the supplied slice does not match the received RTC events.
        pub fn assert_rtc_events(&self, expected: &[RtcEventType]) {
            assert_eq!(self.data.lock().expect("Error aquiring mutex").rtc_events, expected);
        }
    }

    #[cfg(test)]
    mod test {
        use super::*;

        #[test]
        fn log_and_reset_lifecycle_events() {
            let (mut diagnostics, mut monitor) = FakeCobaltDiagnostics::new();
            monitor.assert_lifecycle_events(&[]);

            diagnostics.log_lifecycle_event(LifecycleEventType::ReadFromStash);
            monitor.assert_lifecycle_events(&[LifecycleEventType::ReadFromStash]);

            diagnostics.log_lifecycle_event(LifecycleEventType::StartedUtcFromTimeSource);
            monitor.assert_lifecycle_events(&[
                LifecycleEventType::ReadFromStash,
                LifecycleEventType::StartedUtcFromTimeSource,
            ]);

            monitor.reset();
            monitor.assert_lifecycle_events(&[]);

            diagnostics.log_lifecycle_event(LifecycleEventType::ReadFromStash);
            monitor.assert_lifecycle_events(&[LifecycleEventType::ReadFromStash]);
        }

        #[test]
        fn log_and_reset_rtc_events() {
            let (mut diagnostics, mut monitor) = FakeCobaltDiagnostics::new();
            monitor.assert_rtc_events(&[]);

            diagnostics.log_rtc_event(RtcEventType::ReadFailed);
            monitor.assert_rtc_events(&[RtcEventType::ReadFailed]);

            diagnostics.log_rtc_event(RtcEventType::ReadSucceeded);
            monitor.assert_rtc_events(&[RtcEventType::ReadFailed, RtcEventType::ReadSucceeded]);

            monitor.reset();
            monitor.assert_rtc_events(&[]);

            diagnostics.log_rtc_event(RtcEventType::ReadFailed);
            monitor.assert_rtc_events(&[RtcEventType::ReadFailed]);
        }
    }
}
