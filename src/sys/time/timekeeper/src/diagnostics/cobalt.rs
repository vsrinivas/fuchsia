// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{Diagnostics, Event},
    crate::enums::{InitialClockState, StartClockSource},
    fuchsia_async as fasync,
    fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType},
    parking_lot::Mutex,
    time_metrics_registry::{
        RealTimeClockEventsMetricDimensionEventType as RtcEventType,
        TimekeeperLifecycleEventsMetricDimensionEventType as LifecycleEventType,
        REAL_TIME_CLOCK_EVENTS_METRIC_ID, TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID,
    },
};

/// A connection to the real Cobalt service.
pub struct CobaltDiagnostics {
    /// The wrapped CobaltSender used to log metrics.
    sender: Mutex<CobaltSender>,
    // TODO(fxbug.dev/57677): Move back to an owned fasync::Task instead of detaching the spawned Task
    // once the lifecycle of timekeeper ensures CobaltDiagnostics objects will last long enough
    // to finish their logging.
}

impl CobaltDiagnostics {
    /// Contructs a new `CobaltDiagnostics` instance.
    pub fn new() -> Self {
        let (sender, fut) = CobaltConnector::default()
            .serve(ConnectionType::project_id(time_metrics_registry::PROJECT_ID));
        fasync::Task::spawn(fut).detach();
        Self { sender: Mutex::new(sender) }
    }
}

impl Diagnostics for CobaltDiagnostics {
    fn record(&self, event: Event) {
        match event {
            Event::Initialized { clock_state } => {
                let event = match clock_state {
                    InitialClockState::NotSet => LifecycleEventType::InitializedBeforeUtcStart,
                    InitialClockState::PreviouslySet => {
                        LifecycleEventType::InitializedAfterUtcStart
                    }
                };
                self.sender.lock().log_event(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID, event);
            }
            Event::NetworkAvailable => {}
            Event::InitializeRtc { outcome, .. } => {
                self.sender.lock().log_event(
                    REAL_TIME_CLOCK_EVENTS_METRIC_ID,
                    Into::<RtcEventType>::into(outcome),
                );
            }
            Event::TimeSourceFailed { .. } => {
                // TODO(jsankey): Define and use a Cobalt metric for time source failures.
            }
            Event::TimeSourceStatus { .. } => {}
            Event::SampleRejected { .. } => {
                // TODO(jsankey): Define and use a Cobalt metric for time sample rejections.
            }
            Event::WriteRtc { outcome } => {
                self.sender.lock().log_event(
                    REAL_TIME_CLOCK_EVENTS_METRIC_ID,
                    Into::<RtcEventType>::into(outcome),
                );
            }
            Event::StartClock { source } => {
                let event = match source {
                    StartClockSource::Rtc => LifecycleEventType::StartedUtcFromRtc,
                    StartClockSource::Primary => LifecycleEventType::StartedUtcFromTimeSource,
                };
                self.sender.lock().log_event(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID, event);
            }
            Event::UpdateClock => {}
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::enums::{InitializeRtcOutcome, WriteRtcOutcome},
        fidl_fuchsia_cobalt::{CobaltEvent, Event as EmptyEvent, EventPayload},
        futures::StreamExt,
    };

    #[fasync::run_until_stalled(test)]
    async fn record_initialization_events() {
        let (mpsc_sender, mut mpsc_receiver) = futures::channel::mpsc::channel(1);
        let sender = CobaltSender::new(mpsc_sender);
        let diagnostics = CobaltDiagnostics { sender: Mutex::new(sender) };

        diagnostics.record(Event::Initialized { clock_state: InitialClockState::NotSet });
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID,
                event_codes: vec![LifecycleEventType::InitializedBeforeUtcStart as u32],
                component: None,
                payload: EventPayload::Event(EmptyEvent),
            })
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn record_clock_events() {
        let (mpsc_sender, mut mpsc_receiver) = futures::channel::mpsc::channel(1);
        let sender = CobaltSender::new(mpsc_sender);
        let diagnostics = CobaltDiagnostics { sender: Mutex::new(sender) };

        diagnostics.record(Event::StartClock { source: StartClockSource::Primary });
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID,
                event_codes: vec![LifecycleEventType::StartedUtcFromTimeSource as u32],
                component: None,
                payload: EventPayload::Event(EmptyEvent),
            })
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn record_rtc_events() {
        let (mpsc_sender, mut mpsc_receiver) = futures::channel::mpsc::channel(1);
        let sender = CobaltSender::new(mpsc_sender);
        let diagnostics = CobaltDiagnostics { sender: Mutex::new(sender) };

        diagnostics
            .record(Event::InitializeRtc { outcome: InitializeRtcOutcome::Succeeded, time: None });
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::REAL_TIME_CLOCK_EVENTS_METRIC_ID,
                event_codes: vec![RtcEventType::ReadSucceeded as u32],
                component: None,
                payload: EventPayload::Event(EmptyEvent),
            })
        );

        diagnostics.record(Event::WriteRtc { outcome: WriteRtcOutcome::Failed });
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::REAL_TIME_CLOCK_EVENTS_METRIC_ID,
                event_codes: vec![RtcEventType::WriteFailed as u32],
                component: None,
                payload: EventPayload::Event(EmptyEvent),
            })
        );
    }
}
