// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{Diagnostics, Event},
    crate::enums::{Role, SampleValidationError, TimeSourceError},
    crate::time_source::{
        BoxedPushSource, BoxedPushSourceEventStream, Event as TimeSourceEvent, Sample, TimeSource,
    },
    fidl_fuchsia_time_external::Status,
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_zircon as zx,
    futures::{FutureExt as _, StreamExt as _},
    std::sync::Arc,
    tracing::{error, info, warn},
};

/// Sets the maximum rate at which Timekeeper is willing to accept new updates from a time source in
/// order to limit the Timekeeper resource utilization. This value is also used to apply an upper
/// limit on the monotonic age of time updates.
const MIN_UPDATE_DELAY: zx::Duration = zx::Duration::from_minutes(1);

/// The time to wait before restart after a complete failure of the time source. Many time source
/// failures are likely to repeat so this is useful to limit resource utilization.
const RESTART_DELAY: zx::Duration = zx::Duration::from_minutes(5);

/// How frequently a source that declares itself to be healthy needs to produce updates in order to
/// remain selected. Sources are restarted if they fail to produce updates faster than this.
const SOURCE_KEEPALIVE: zx::Duration = zx::Duration::from_minutes(60);

/// A provider of monotonic times.
pub trait MonotonicProvider: Send + Sync {
    /// Returns the current monotonic time.
    fn now(&mut self) -> zx::Time;
}

/// A provider of true monotonic times from the kernel.
pub struct KernelMonotonicProvider();

impl MonotonicProvider for KernelMonotonicProvider {
    fn now(&mut self) -> zx::Time {
        zx::Time::get_monotonic()
    }
}

/// A wrapper that provide common interface for other time source managers.
///
/// In the future `TimeSourceManager` will also handle multiple time sources and the selection
/// between them. Meaning it will manage up to three sources.
pub struct TimeSourceManager<D: Diagnostics, M: MonotonicProvider> {
    /// Manager for the time source.
    manager: TimeManager<D, M>,
}

/// TODO(fxb/110804): Add PullSourceManager.
#[allow(dead_code)]
enum TimeManager<D: Diagnostics, M: MonotonicProvider> {
    Push(PushSourceManager<D, M>),
    Pull(),
}

/// A wrapper that launches a time source component, uses PushSource to obtain time samples,
/// validates them from the source, and handles relaunching the source in the case of failures.
struct PushSourceManager<D: Diagnostics, M: MonotonicProvider> {
    /// The role of the time source being managed.
    role: Role,
    /// The backstop time that samples must not come before.
    backstop: zx::Time,
    /// Whether the time source restart delay and minimum update delay should be enabled.
    delays_enabled: bool,
    /// A source of monotonic time.
    monotonic: M,
    /// The time source to be managed.
    time_source: BoxedPushSource,

    /// A diagnostics implementation for recording events of note.
    diagnostics: Arc<D>,

    /// The active event stream, present when the source is currently running.
    event_stream: Option<BoxedPushSourceEventStream>,
    /// The most recent status received from the time source in its current execution.
    last_status: Option<Status>,
    /// The monotonic time at which the most recently accepted Sample arrived.
    last_accepted_sample_arrival: Option<zx::Time>,
}

impl<D: Diagnostics, M: MonotonicProvider> PushSourceManager<D, M> {
    /// Returns the next valid sample from the Push timesource.
    async fn next_sample_from_push(&mut self) -> Sample {
        loop {
            // Extract the event stream from self if one exists and attempt to start one if not.
            let mut event_stream = match self.event_stream.take() {
                Some(event_stream) => event_stream,
                None => match self.time_source.watch().await {
                    Ok(event_stream) => event_stream,
                    Err(err) => {
                        error!("Error launching {:?} time source: {:?}", self.role, err);
                        self.record_time_source_failure(TimeSourceError::LaunchFailed);
                        if self.delays_enabled {
                            fasync::Timer::new(fasync::Time::after(RESTART_DELAY)).await;
                        }
                        continue;
                    }
                },
            };

            // Try to wait for a valid sample from the event stream, inserting the event stream
            // back into self for next time if we're successful.
            match self.next_sample_from_stream(&mut event_stream).await {
                Ok(sample) => {
                    self.event_stream.replace(event_stream);
                    return sample;
                }
                Err(failure) => {
                    self.record_time_source_failure(failure);
                    self.last_status = None;
                    if self.delays_enabled {
                        fasync::Timer::new(fasync::Time::after(RESTART_DELAY)).await;
                    }
                }
            }
        }
    }

    /// Record a time source failure via diagnostics.
    fn record_time_source_failure(&self, error: TimeSourceError) {
        self.diagnostics.record(Event::TimeSourceFailed { role: self.role, error });
    }

    /// Returns the next valid sample from the supplied stream, or an error if the stream
    /// encounters a terminal error. The monotonic provider will be queried exactly once to
    /// validate every `TimeSourceEvent::Sample` received.
    async fn next_sample_from_stream(
        &mut self,
        event_stream: &mut BoxedPushSourceEventStream,
    ) -> Result<Sample, TimeSourceError> {
        loop {
            // Time sources whose current status is OK must send new samples (or state
            // changes) within SOURCE_KEEPALIVE. This doesn't apply to sources that are not
            // OK (e.g. those waiting indefinitely for network availability).
            let timeout = match self.last_status {
                Some(Status::Ok) => zx::Time::after(SOURCE_KEEPALIVE),
                _ => zx::Time::INFINITE,
            };

            let event = event_stream
                .next()
                .map(|res| res.ok_or(TimeSourceError::StreamFailed))
                .on_timeout(timeout, || Err(TimeSourceError::SampleTimeOut))
                .await
                .map_err(|err| {
                    warn!("Error polling stream on {:?}: {:?}", self.role, err);
                    err
                })?
                .map_err(|err| {
                    warn!("Error calling watch on {:?}: {:?}", self.role, err);
                    TimeSourceError::CallFailed
                })?;

            match event {
                TimeSourceEvent::StatusChange { status } if self.last_status == Some(status) => {
                    info!("Ignoring repeated {:?} state of {:?}", self.role, status);
                }
                TimeSourceEvent::StatusChange { status } => {
                    info!("{:?} changed state to {:?}", self.role, status);
                    self.diagnostics.record(Event::TimeSourceStatus { role: self.role, status });
                    self.last_status = Some(status);
                }
                TimeSourceEvent::Sample(sample) => match self.validate_sample(&sample) {
                    Ok(arrival) => {
                        // The current API leaves the potential for a race condition between a
                        // source declaring itself OK and sending the first sample. Since the
                        // non-OK states describe reasons a time source is incapable of sending
                        // samples, we mark a source as OK if we receive a valid sample from any
                        // other state.
                        if self.last_status != Some(Status::Ok) {
                            info!("{:?} setting state to OK on receipt of valid sample", self.role);
                            self.diagnostics.record(Event::TimeSourceStatus {
                                role: self.role,
                                status: Status::Ok,
                            });
                            self.last_status = Some(Status::Ok);
                        }
                        self.last_accepted_sample_arrival = Some(arrival);
                        return Ok(sample);
                    }
                    Err(error) => {
                        error!("Rejected invalid sample from {:?}: {:?}", self.role, error);
                        self.diagnostics.record(Event::SampleRejected { role: self.role, error });
                    }
                },
            }
        }
    }

    /// Validates the supplied time sample against the current state. Returns the current
    /// monotonic time on success so it may be used as an arrival time.
    fn validate_sample(&mut self, sample: &Sample) -> Result<zx::Time, SampleValidationError> {
        let current_monotonic = self.monotonic.now();
        let earliest_allowed_arrival = match self.last_accepted_sample_arrival {
            Some(previous_arrival) if self.delays_enabled => previous_arrival + MIN_UPDATE_DELAY,
            _ => zx::Time::INFINITE_PAST,
        };

        if sample.utc < self.backstop {
            Err(SampleValidationError::BeforeBackstop)
        } else if sample.monotonic > current_monotonic {
            Err(SampleValidationError::MonotonicInFuture)
        } else if sample.monotonic < current_monotonic - MIN_UPDATE_DELAY {
            Err(SampleValidationError::MonotonicTooOld)
        } else if current_monotonic < earliest_allowed_arrival {
            Err(SampleValidationError::TooCloseToPrevious)
        } else {
            Ok(current_monotonic)
        }
    }
}

impl<D: Diagnostics> TimeSourceManager<D, KernelMonotonicProvider> {
    /// Constructs a new `TimeSourceManager` that reads monotonic times from the kernel.
    pub fn new(
        backstop: zx::Time,
        role: Role,
        time_source: TimeSource,
        diagnostics: Arc<D>,
    ) -> Self {
        let manager = match time_source {
            TimeSource::Push(time_source) => TimeManager::Push(PushSourceManager {
                backstop,
                delays_enabled: true,
                role,
                time_source,
                diagnostics,
                monotonic: KernelMonotonicProvider(),
                event_stream: None,
                last_status: None,
                last_accepted_sample_arrival: None,
            }),
            TimeSource::Pull(_) => unimplemented!(),
        };
        TimeSourceManager { manager }
    }

    /// Constructs a new `TimeSourceManager` that reads monotonic times from the kernel and has
    /// the restart delay and minimum update delay set to zero. This makes the behavior more
    /// amenable to use in tests.
    pub fn new_with_delays_disabled(
        backstop: zx::Time,
        role: Role,
        time_source: TimeSource,
        diagnostics: Arc<D>,
    ) -> Self {
        let mut manager = Self::new(backstop, role, time_source, diagnostics);
        match &mut manager.manager {
            TimeManager::Push(m) => m.delays_enabled = false,
            TimeManager::Pull() => unimplemented!(),
        }
        manager
    }
}

impl<D: Diagnostics, M: MonotonicProvider> TimeSourceManager<D, M> {
    /// Returns the `Role` of the time source being managed.
    ///
    /// Note: This method is viable while the `TimeSourceManager` is managing a single time source.
    ///       Once fallback and gating sources are added role will be moved to a property of each
    ///       time sample and this method will be removed.
    pub fn role(&self) -> Role {
        match &self.manager {
            TimeManager::Push(m) => m.role,
            TimeManager::Pull() => unimplemented!(),
        }
    }

    /// Returns the next valid sample from the time source.
    pub async fn next_sample(&mut self) -> Sample {
        match &mut self.manager {
            TimeManager::Push(m) => m.next_sample_from_push().await,
            TimeManager::Pull() => unimplemented!(),
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::diagnostics::FakeDiagnostics,
        crate::enums::{SampleValidationError as SVE, TimeSourceError as TSE},
        crate::time_source::FakePushTimeSource,
        anyhow::anyhow,
    };

    const BACKSTOP_FACTOR: i64 = 100;
    const TEST_ROLE: Role = Role::Monitor;
    const STD_DEV: zx::Duration = zx::Duration::from_millis(22);

    macro_rules! assert_push_manager {
        ($manager:expr) => {{
            if let TimeManager::Push(m) = $manager.manager {
                m
            } else {
                panic!("Expected TimeManager::Push.")
            }
        }};
    }

    /// A provider of artificial monotonic times that increment by a fixed duration each call.
    struct FakeMonotonicProvider {
        increment: zx::Duration,
        last_time: zx::Time,
    }

    impl FakeMonotonicProvider {
        /// Constructs a new `FakeMonotonicProvider` that increments by `increment` on each call.
        pub fn new(increment: zx::Duration) -> Self {
            FakeMonotonicProvider { increment, last_time: zx::Time::ZERO }
        }
    }

    impl MonotonicProvider for FakeMonotonicProvider {
        fn now(&mut self) -> zx::Time {
            self.last_time += self.increment;
            self.last_time
        }
    }

    /// Create a new `TimeSourceManager` using the standard backstop time and role, a monotonic time
    /// that increments by `MIN_UPDATE_DELAY` per sample, and the supplied time source and
    /// diagnostics.
    fn create_manager(
        time_source: FakePushTimeSource,
        diagnostics: Arc<FakeDiagnostics>,
    ) -> TimeSourceManager<FakeDiagnostics, FakeMonotonicProvider> {
        let manager = TimeManager::Push(PushSourceManager {
            backstop: zx::Time::ZERO + (MIN_UPDATE_DELAY * BACKSTOP_FACTOR),
            delays_enabled: true,
            monotonic: FakeMonotonicProvider::new(MIN_UPDATE_DELAY),
            role: TEST_ROLE,
            time_source: Box::new(time_source),
            diagnostics,
            event_stream: None,
            last_status: None,
            last_accepted_sample_arrival: None,
        });
        TimeSourceManager { manager }
    }

    /// Create a new `TimeSourceManager` using the standard backstop time and role, a monotonic time
    /// that increments by `MIN_UPDATE_DELAY` per sample, and the supplied time source and
    /// diagnostics. Restart and min update delays are disabled.
    fn create_manager_delays_disabled(
        time_source: FakePushTimeSource,
        diagnostics: Arc<FakeDiagnostics>,
    ) -> TimeSourceManager<FakeDiagnostics, FakeMonotonicProvider> {
        let manager = TimeManager::Push(PushSourceManager {
            backstop: zx::Time::ZERO + (MIN_UPDATE_DELAY * BACKSTOP_FACTOR),
            delays_enabled: false,
            monotonic: FakeMonotonicProvider::new(MIN_UPDATE_DELAY),
            role: TEST_ROLE,
            time_source: Box::new(time_source),
            diagnostics,
            event_stream: None,
            last_status: None,
            last_accepted_sample_arrival: None,
        });
        TimeSourceManager { manager }
    }

    /// Creates a new time sample from the supplied times. Both UTC and Monotonic are supplied as
    /// a factor to multiply by MIN_UPDATE_DELAY, which is the minimum interval the manager would
    /// accept between samples.rate at hence the rate we choose our fake monotonic clock to tick at.
    fn create_sample(utc_factor: i64, monotonic_factor: i64) -> Sample {
        Sample {
            utc: zx::Time::ZERO + (MIN_UPDATE_DELAY * utc_factor),
            monotonic: zx::Time::ZERO + (MIN_UPDATE_DELAY * monotonic_factor),
            std_dev: STD_DEV,
        }
    }

    #[fuchsia::test]
    fn role_accessor() {
        let time_source = FakePushTimeSource::failing();
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let manager = create_manager(time_source, diagnostics);
        assert_eq!(manager.role(), TEST_ROLE);
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn event_in_future() {
        let time_source = FakePushTimeSource::events(vec![
            TimeSourceEvent::StatusChange { status: Status::Ok },
            TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 1, 1)),
            // Should be ignored since monotonic is in the future
            TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 2, 20)),
            TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 3, 3)),
        ]);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut manager = create_manager(time_source, Arc::clone(&diagnostics));

        assert_eq!(manager.next_sample().await, create_sample(BACKSTOP_FACTOR + 1, 1));
        assert_eq!(manager.next_sample().await, create_sample(BACKSTOP_FACTOR + 3, 3));

        let push_manager = assert_push_manager!(manager);

        assert_eq!(
            push_manager.last_accepted_sample_arrival,
            Some(zx::Time::ZERO + MIN_UPDATE_DELAY * 3)
        );

        diagnostics.assert_events(&[
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
            Event::SampleRejected { role: TEST_ROLE, error: SVE::MonotonicInFuture },
        ]);
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn sample_implies_ok() {
        let time_source = FakePushTimeSource::events(vec![
            // Should be accepted even though time source is not currently OK.
            TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 1, 1)),
            // Should not be recorded since we moved the source to OK on receiving the sample.
            TimeSourceEvent::StatusChange { status: Status::Ok },
            TimeSourceEvent::StatusChange { status: Status::Network },
            // Should be accepted even though time source is not curently OK.
            TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 2, 2)),
        ]);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut manager = create_manager(time_source, Arc::clone(&diagnostics));

        assert_eq!(manager.next_sample().await, create_sample(BACKSTOP_FACTOR + 1, 1));
        assert_eq!(manager.next_sample().await, create_sample(BACKSTOP_FACTOR + 2, 2));

        let push_manager = assert_push_manager!(manager);
        assert_eq!(push_manager.last_status, Some(Status::Ok));

        diagnostics.assert_events(&[
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Network },
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
        ]);
    }

    #[fuchsia::test]
    async fn restart_on_watch_error() {
        let time_source = FakePushTimeSource::result_collections(vec![
            vec![
                Ok(TimeSourceEvent::StatusChange { status: Status::Ok }),
                Ok(TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 1, 1))),
                Err(anyhow!("Walked through wet cement")),
                // Should be ignored since Err caused restart.
                Ok(TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 2, 2))),
            ],
            vec![
                Ok(TimeSourceEvent::StatusChange { status: Status::Ok }),
                Ok(TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 3, 2))),
                Ok(TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 4, 3))),
            ],
        ]);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut manager = create_manager_delays_disabled(time_source, Arc::clone(&diagnostics));

        assert_eq!(manager.next_sample().await, create_sample(BACKSTOP_FACTOR + 1, 1));
        assert_eq!(manager.next_sample().await, create_sample(BACKSTOP_FACTOR + 3, 2));
        assert_eq!(manager.next_sample().await, create_sample(BACKSTOP_FACTOR + 4, 3));

        diagnostics.assert_events(&[
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
            Event::TimeSourceFailed { role: TEST_ROLE, error: TSE::CallFailed },
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
        ]);
    }

    #[fuchsia::test]
    async fn restart_on_channel_close() {
        let time_source = FakePushTimeSource::event_collections(vec![
            vec![
                TimeSourceEvent::StatusChange { status: Status::Ok },
                TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 1, 1)),
            ],
            vec![
                TimeSourceEvent::StatusChange { status: Status::Ok },
                TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 2, 2)),
            ],
        ]);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut manager = create_manager_delays_disabled(time_source, Arc::clone(&diagnostics));

        assert_eq!(manager.next_sample().await, create_sample(BACKSTOP_FACTOR + 1, 1));
        assert_eq!(manager.next_sample().await, create_sample(BACKSTOP_FACTOR + 2, 2));

        diagnostics.assert_events(&[
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
            Event::TimeSourceFailed { role: TEST_ROLE, error: TSE::StreamFailed },
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
        ]);
    }

    #[fuchsia::test]
    async fn restart_on_launch_failure() {
        let time_source = FakePushTimeSource::failing().into();
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut manager = TimeSourceManager::new(
            zx::Time::ZERO,
            TEST_ROLE,
            time_source,
            Arc::clone(&diagnostics),
        );

        // Calling next sample on this manager with the restart delay enabled should lead to
        // failed launch and then a few minute cooldown period before relaunch. We test for this by
        // verifying a short timeout triggered.
        assert_eq!(
            manager
                .next_sample()
                .map(|_| true)
                .on_timeout(zx::Time::after(zx::Duration::from_millis(50)), || false)
                .await,
            false
        );

        diagnostics.assert_events(&[Event::TimeSourceFailed {
            role: TEST_ROLE,
            error: TSE::LaunchFailed,
        }]);
    }

    #[fuchsia::test]
    fn validate_sample_failures() {
        let manager =
            create_manager(FakePushTimeSource::failing(), Arc::new(FakeDiagnostics::new()));
        let mut push_manager = assert_push_manager!(manager);
        push_manager.last_status = Some(Status::Ok);

        // The monotonic our manager sees will start at a factor of 1 and increment by 1 each time
        // we try to validate a sample.
        assert_eq!(
            push_manager.validate_sample(&create_sample(BACKSTOP_FACTOR, 1)),
            Ok(zx::Time::ZERO + MIN_UPDATE_DELAY)
        );
        assert_eq!(
            push_manager.validate_sample(&create_sample(BACKSTOP_FACTOR - 1, 2)),
            Err(SVE::BeforeBackstop)
        );
        assert_eq!(
            push_manager.validate_sample(&create_sample(BACKSTOP_FACTOR, 0)),
            Err(SVE::MonotonicTooOld)
        );
        assert_eq!(
            push_manager.validate_sample(&create_sample(BACKSTOP_FACTOR, 100)),
            Err(SVE::MonotonicInFuture)
        );
        // On the next call the monontonic should be a factor of 5, trick the manager into thinking
        // it already accepted an update at 4.5
        push_manager.last_accepted_sample_arrival =
            Some(zx::Time::from_nanos(MIN_UPDATE_DELAY.into_nanos() / 2 * 9));
        assert_eq!(
            push_manager.validate_sample(&create_sample(BACKSTOP_FACTOR, 5)),
            Err(SVE::TooCloseToPrevious)
        );
        // But if we disable delays an accepted update of 5.5 at a monotonic of 6 is accepted.
        push_manager.delays_enabled = false;
        push_manager.last_accepted_sample_arrival =
            Some(zx::Time::from_nanos(MIN_UPDATE_DELAY.into_nanos() / 2 * 11));
        assert_eq!(
            push_manager.validate_sample(&create_sample(BACKSTOP_FACTOR, 6)),
            Ok(zx::Time::ZERO + MIN_UPDATE_DELAY * 6)
        );
    }
}
