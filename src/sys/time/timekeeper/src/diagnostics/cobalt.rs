// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics::{Diagnostics, Event},
        enums::{ClockCorrectionStrategy, InitialClockState, StartClockSource, Track},
        MonitorTrack, PrimaryTrack, TimeSource,
    },
    fuchsia_async as fasync,
    fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType},
    fuchsia_zircon as zx,
    parking_lot::Mutex,
    std::sync::Arc,
    time_metrics_registry::{
        RealTimeClockEventsMetricDimensionEventType as RtcEvent,
        TimeMetricDimensionDirection as Direction, TimeMetricDimensionExperiment as Experiment,
        TimeMetricDimensionRole as CobaltRole, TimeMetricDimensionTrack as CobaltTrack,
        TimekeeperLifecycleEventsMetricDimensionEventType as LifecycleEvent,
        TimekeeperTimeSourceEventsMetricDimensionEventType as TimeSourceEvent,
        TimekeeperTrackEventsMetricDimensionEventType as TrackEvent,
        REAL_TIME_CLOCK_EVENTS_METRIC_ID, TIMEKEEPER_CLOCK_CORRECTION_METRIC_ID,
        TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID, TIMEKEEPER_MONITOR_DIFFERENCE_METRIC_ID,
        TIMEKEEPER_SQRT_COVARIANCE_METRIC_ID, TIMEKEEPER_TIME_SOURCE_EVENTS_METRIC_ID,
        TIMEKEEPER_TRACK_EVENTS_METRIC_ID,
    },
};

/// The period duration in micros. This field is required for Cobalt 1.0 EVENT_COUNT but not used.
const PERIOD_DURATION: i64 = 0;

/// A connection to the real Cobalt service.
pub struct CobaltDiagnostics {
    /// The wrapped CobaltSender used to log metrics.
    sender: Mutex<CobaltSender>,
    /// The experiment to record on all experiment-based events.
    experiment: Experiment,
    /// The UTC clock used in the primary track.
    primary_clock: Arc<zx::Clock>,
    /// The UTC clock used in the monitor track.
    monitor_clock: Option<Arc<zx::Clock>>,
    // TODO(fxbug.dev/57677): Move back to an owned fasync::Task instead of detaching the spawned
    // Task once the lifecycle of timekeeper ensures CobaltDiagnostics objects will last long enough
    // to finish their logging.
}

impl CobaltDiagnostics {
    /// Contructs a new `CobaltDiagnostics` instance.
    pub(crate) fn new<T: TimeSource>(
        experiment: Experiment,
        primary: &PrimaryTrack<T>,
        optional_monitor: &Option<MonitorTrack<T>>,
    ) -> Self {
        let (sender, fut) = CobaltConnector::default()
            .serve(ConnectionType::project_id(time_metrics_registry::PROJECT_ID));
        fasync::Task::spawn(fut).detach();
        Self {
            sender: Mutex::new(sender),
            experiment,
            primary_clock: Arc::clone(&primary.clock),
            monitor_clock: optional_monitor.as_ref().map(|track| Arc::clone(&track.clock)),
        }
    }

    /// Records an update to the estimate, including an event and a covariance report.
    fn record_estimate_update(&self, track: Track, sqrt_covariance: zx::Duration) {
        let mut locked_sender = self.sender.lock();
        let cobalt_track = Into::<CobaltTrack>::into(track);
        locked_sender.log_event_count(
            TIMEKEEPER_TRACK_EVENTS_METRIC_ID,
            (TrackEvent::EstimatedOffsetUpdated, cobalt_track, self.experiment),
            PERIOD_DURATION,
            1,
        );
        locked_sender.log_event_count(
            TIMEKEEPER_SQRT_COVARIANCE_METRIC_ID,
            (Into::<CobaltTrack>::into(track), self.experiment),
            PERIOD_DURATION,
            // Unfortunately Cobalt does not follow the standard of nanoseconds everywhere.
            sqrt_covariance.into_micros(),
        );
    }

    /// Records a clock correction, including an event and a report on the magnitude of change.
    fn record_clock_correction(
        &self,
        track: Track,
        correction: zx::Duration,
        strategy: ClockCorrectionStrategy,
    ) {
        let mut locked_sender = self.sender.lock();
        let cobalt_track = Into::<CobaltTrack>::into(track);
        let direction =
            if correction.into_nanos() >= 0 { Direction::Positive } else { Direction::Negative };
        locked_sender.log_event_count(
            TIMEKEEPER_TRACK_EVENTS_METRIC_ID,
            (Into::<TrackEvent>::into(strategy), cobalt_track, self.experiment),
            PERIOD_DURATION,
            1,
        );
        locked_sender.log_event_count(
            TIMEKEEPER_CLOCK_CORRECTION_METRIC_ID,
            (direction, cobalt_track, self.experiment),
            PERIOD_DURATION,
            // Unfortunately Cobalt does not follow the standard of nanoseconds everywhere.
            correction.into_micros().abs(),
        );
    }

    /// Records relevant information following a clock update.
    ///
    /// Currently this only records the difference between the monitor and primary clocks when the
    /// monitor clock is updated.
    fn record_clock_update(&self, track: Track) {
        if track != Track::Monitor {
            return;
        }
        let primary = match self.primary_clock.read() {
            Ok(utc) => utc,
            Err(_) => return,
        };
        let monitor = match self.monitor_clock.as_ref().map(|clk| clk.read()) {
            Some(Ok(utc)) => utc,
            _ => return,
        };
        let direction = if monitor >= primary { Direction::Positive } else { Direction::Negative };
        self.sender.lock().log_event_count(
            TIMEKEEPER_MONITOR_DIFFERENCE_METRIC_ID,
            (direction, self.experiment),
            PERIOD_DURATION,
            // Unfortunately Cobalt does not follow the standard of nanoseconds everywhere.
            (monitor - primary).into_micros().abs(),
        );
    }
}

impl Diagnostics for CobaltDiagnostics {
    fn record(&self, event: Event) {
        match event {
            Event::Initialized { clock_state } => {
                let event = match clock_state {
                    InitialClockState::NotSet => LifecycleEvent::InitializedBeforeUtcStart,
                    InitialClockState::PreviouslySet => LifecycleEvent::InitializedAfterUtcStart,
                };
                self.sender.lock().log_event(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID, event);
            }
            Event::NetworkAvailable => {}
            Event::InitializeRtc { outcome, .. } => {
                self.sender
                    .lock()
                    .log_event(REAL_TIME_CLOCK_EVENTS_METRIC_ID, Into::<RtcEvent>::into(outcome));
            }
            Event::TimeSourceFailed { role, error } => {
                let event = Into::<TimeSourceEvent>::into(error);
                self.sender.lock().log_event_count(
                    TIMEKEEPER_TIME_SOURCE_EVENTS_METRIC_ID,
                    (event, Into::<CobaltRole>::into(role), self.experiment),
                    PERIOD_DURATION,
                    1,
                );
            }
            Event::TimeSourceStatus { .. } => {}
            Event::SampleRejected { role, error } => {
                let event = Into::<TimeSourceEvent>::into(error);
                self.sender.lock().log_event_count(
                    TIMEKEEPER_TIME_SOURCE_EVENTS_METRIC_ID,
                    (event, Into::<CobaltRole>::into(role), self.experiment),
                    PERIOD_DURATION,
                    1,
                );
            }
            Event::EstimateUpdated { track, sqrt_covariance, .. } => {
                self.record_estimate_update(track, sqrt_covariance);
            }
            Event::ClockCorrection { track, correction, strategy } => {
                self.record_clock_correction(track, correction, strategy);
            }
            Event::WriteRtc { outcome } => {
                self.sender
                    .lock()
                    .log_event(REAL_TIME_CLOCK_EVENTS_METRIC_ID, Into::<RtcEvent>::into(outcome));
            }
            Event::StartClock { track, source } => {
                if track == Track::Primary {
                    let event = match source {
                        StartClockSource::Rtc => LifecycleEvent::StartedUtcFromRtc,
                        StartClockSource::External(_) => LifecycleEvent::StartedUtcFromTimeSource,
                    };
                    self.sender.lock().log_event(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID, event);
                }
            }
            Event::UpdateClock { track, .. } => {
                self.record_clock_update(track);
            }
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::enums::{
            ClockUpdateReason, InitializeRtcOutcome, Role, SampleValidationError, TimeSourceError,
            WriteRtcOutcome,
        },
        fidl_fuchsia_cobalt::{CobaltEvent, CountEvent, Event as EmptyEvent, EventPayload},
        futures::{channel::mpsc, FutureExt, StreamExt},
        test_util::{assert_geq, assert_leq},
    };

    const TEST_EXPERIMENT: Experiment = Experiment::B;
    const MONITOR_OFFSET: zx::Duration = zx::Duration::from_seconds(444);

    fn create_clock(time: zx::Time) -> Arc<zx::Clock> {
        let clk = zx::Clock::create(zx::ClockOpts::empty(), None).unwrap();
        clk.update(zx::ClockUpdate::new().value(time)).unwrap();
        Arc::new(clk)
    }

    /// Creates a test CobaltDiagnostics and an mpsc receiver that may be used to verify the
    /// events it sends. The primary and monitor clocks will have a difference of MONITOR_OFFSET.
    fn create_test_object() -> (CobaltDiagnostics, mpsc::Receiver<CobaltEvent>) {
        let (mpsc_sender, mpsc_receiver) = futures::channel::mpsc::channel(1);
        let sender = CobaltSender::new(mpsc_sender);
        let diagnostics = CobaltDiagnostics {
            sender: Mutex::new(sender),
            experiment: TEST_EXPERIMENT,
            primary_clock: create_clock(zx::Time::ZERO),
            monitor_clock: Some(create_clock(zx::Time::ZERO + MONITOR_OFFSET)),
        };
        (diagnostics, mpsc_receiver)
    }

    /// Creates an `EventPayload` containing the supplied count.
    fn event_count_payload(count: i64) -> EventPayload {
        EventPayload::EventCount(CountEvent { period_duration_micros: 0, count })
    }

    #[fasync::run_until_stalled(test)]
    async fn record_initialization_events() {
        let (diagnostics, mut mpsc_receiver) = create_test_object();

        diagnostics.record(Event::Initialized { clock_state: InitialClockState::NotSet });
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID,
                event_codes: vec![LifecycleEvent::InitializedBeforeUtcStart as u32],
                component: None,
                payload: EventPayload::Event(EmptyEvent),
            })
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn record_clock_events() {
        let (diagnostics, mut mpsc_receiver) = create_test_object();

        diagnostics.record(Event::StartClock {
            track: Track::Monitor,
            source: StartClockSource::External(Role::Monitor),
        });
        diagnostics.record(Event::StartClock {
            track: Track::Primary,
            source: StartClockSource::External(Role::Primary),
        });
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID,
                event_codes: vec![LifecycleEvent::StartedUtcFromTimeSource as u32],
                component: None,
                payload: EventPayload::Event(EmptyEvent),
            })
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn record_rtc_events() {
        let (diagnostics, mut mpsc_receiver) = create_test_object();

        diagnostics
            .record(Event::InitializeRtc { outcome: InitializeRtcOutcome::Succeeded, time: None });
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::REAL_TIME_CLOCK_EVENTS_METRIC_ID,
                event_codes: vec![RtcEvent::ReadSucceeded as u32],
                component: None,
                payload: EventPayload::Event(EmptyEvent),
            })
        );

        diagnostics.record(Event::WriteRtc { outcome: WriteRtcOutcome::Failed });
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::REAL_TIME_CLOCK_EVENTS_METRIC_ID,
                event_codes: vec![RtcEvent::WriteFailed as u32],
                component: None,
                payload: EventPayload::Event(EmptyEvent),
            })
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn record_time_source_events() {
        let (diagnostics, mut mpsc_receiver) = create_test_object();

        diagnostics.record(Event::SampleRejected {
            role: Role::Primary,
            error: SampleValidationError::MonotonicTooOld,
        });
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::TIMEKEEPER_TIME_SOURCE_EVENTS_METRIC_ID,
                event_codes: vec![
                    TimeSourceEvent::SampleRejectedMonotonicTooOld as u32,
                    CobaltRole::Primary as u32,
                    TEST_EXPERIMENT as u32
                ],
                component: None,
                payload: event_count_payload(1),
            })
        );

        diagnostics.record(Event::TimeSourceFailed {
            role: Role::Monitor,
            error: TimeSourceError::CallFailed,
        });
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::TIMEKEEPER_TIME_SOURCE_EVENTS_METRIC_ID,
                event_codes: vec![
                    TimeSourceEvent::RestartedCallFailed as u32,
                    CobaltRole::Monitor as u32,
                    TEST_EXPERIMENT as u32
                ],
                component: None,
                payload: event_count_payload(1),
            })
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn record_time_track_events() {
        let (diagnostics, mut mpsc_receiver) = create_test_object();

        diagnostics.record(Event::EstimateUpdated {
            track: Track::Primary,
            offset: zx::Duration::from_seconds(333),
            sqrt_covariance: zx::Duration::from_micros(55555),
        });
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::TIMEKEEPER_TRACK_EVENTS_METRIC_ID,
                event_codes: vec![
                    TrackEvent::EstimatedOffsetUpdated as u32,
                    CobaltTrack::Primary as u32,
                    TEST_EXPERIMENT as u32
                ],
                component: None,
                payload: event_count_payload(1),
            })
        );
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::TIMEKEEPER_SQRT_COVARIANCE_METRIC_ID,
                event_codes: vec![CobaltTrack::Primary as u32, TEST_EXPERIMENT as u32],
                component: None,
                payload: event_count_payload(55555),
            })
        );

        diagnostics.record(Event::ClockCorrection {
            track: Track::Monitor,
            correction: zx::Duration::from_micros(-777),
            strategy: ClockCorrectionStrategy::NominalRateSlew,
        });
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::TIMEKEEPER_TRACK_EVENTS_METRIC_ID,
                event_codes: vec![
                    TrackEvent::CorrectionByNominalRateSlew as u32,
                    CobaltTrack::Monitor as u32,
                    TEST_EXPERIMENT as u32
                ],
                component: None,
                payload: event_count_payload(1),
            })
        );
        assert_eq!(
            mpsc_receiver.next().await,
            Some(CobaltEvent {
                metric_id: time_metrics_registry::TIMEKEEPER_CLOCK_CORRECTION_METRIC_ID,
                event_codes: vec![
                    Direction::Negative as u32,
                    CobaltTrack::Monitor as u32,
                    TEST_EXPERIMENT as u32
                ],
                component: None,
                payload: event_count_payload(777),
            })
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn record_update_clock_events() {
        let (diagnostics, mut mpsc_receiver) = create_test_object();

        // Updates to the primary track should be ignored.
        diagnostics.record(Event::UpdateClock {
            track: Track::Primary,
            reason: ClockUpdateReason::TimeStep,
        });
        assert!(mpsc_receiver.next().now_or_never().is_none());

        // Updates to the monitor track should lead to a difference report.
        // Unfortunately its cumbersome to verify since we don't know the exact clock difference.
        diagnostics.record(Event::UpdateClock {
            track: Track::Monitor,
            reason: ClockUpdateReason::TimeStep,
        });
        let event = mpsc_receiver.next().await.unwrap();
        assert_eq!(event.metric_id, TIMEKEEPER_MONITOR_DIFFERENCE_METRIC_ID);
        assert_eq!(event.event_codes, vec![Direction::Positive as u32, TEST_EXPERIMENT as u32]);
        match event.payload {
            EventPayload::EventCount(CountEvent { period_duration_micros, count }) => {
                assert_eq!(period_duration_micros, 0);
                assert_geq!(count, MONITOR_OFFSET.into_micros() - 1000);
                assert_leq!(count, MONITOR_OFFSET.into_micros() + 1000);
            }
            _ => panic!("monitor clock update did not produce event count payload"),
        }
    }
}
