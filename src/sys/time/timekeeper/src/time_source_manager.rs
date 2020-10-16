// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{Diagnostics, Event},
    crate::enums::{Role, SampleValidationError, TimeSourceError},
    crate::time_source::{Event as TimeSourceEvent, Sample, TimeSource},
    fidl_fuchsia_time_external::Status,
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_zircon as zx,
    futures::{FutureExt as _, StreamExt as _},
    log::{info, warn},
    std::sync::Arc,
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

/// A wrapper that launches a time source, validates time updates from the source, and handles
/// relaunching the source in the case of failures.
///
/// In the future `TimeSourceManager` will also handle multiple time sources and the selection
/// between them. Meaning it will manage up to three sources.
pub struct TimeSourceManager<T: TimeSource, D: Diagnostics, M: MonotonicProvider> {
    /// The backstop time that samples must not come before.
    backstop: zx::Time,
    /// Whether the time source restart delay and minimum update delay should be enabled.
    delays_enabled: bool,
    /// A source of monotonic time.
    monotonic: M,
    /// The role of the time source being managed.
    role: Role,
    /// The time source to be managed.
    time_source: T,
    /// A diagnostics implementation for recording events of note.
    diagnostics: Arc<D>,

    /// The active event stream, present when the source is currently running.
    event_stream: Option<T::EventStream>,
    /// The most recent status received from the time source in its current execution.
    last_status: Option<Status>,
    /// The monotonic time at which the most recently accepted Sample arrived.
    last_accepted_sample_arrival: Option<zx::Time>,
}

impl<T: TimeSource, D: Diagnostics> TimeSourceManager<T, D, KernelMonotonicProvider> {
    /// Constructs a new `TimeSourceManager` that reads monotonic times from the kernel.
    pub fn new(backstop: zx::Time, role: Role, time_source: T, diagnostics: Arc<D>) -> Self {
        TimeSourceManager {
            backstop,
            delays_enabled: true,
            monotonic: KernelMonotonicProvider(),
            role,
            time_source,
            diagnostics,
            event_stream: None,
            last_status: None,
            last_accepted_sample_arrival: None,
        }
    }

    /// Constructs a new `TimeSourceManager` that reads monotonic times from the kernel and has
    /// the restart delay and minimum update delay set to zero. This makes the behavior more
    /// amenable to use in tests.
    #[allow(unused)]
    pub fn new_with_delays_disabled(
        backstop: zx::Time,
        role: Role,
        time_source: T,
        diagnostics: Arc<D>,
    ) -> Self {
        let mut manager = Self::new(backstop, role, time_source, diagnostics);
        manager.delays_enabled = false;
        manager
    }
}

impl<T: TimeSource, D: Diagnostics, M: MonotonicProvider> TimeSourceManager<T, D, M> {
    /// Warms up the time source by launching the time source but does not yet pull any events.
    /// TODO(jsankey): Remove this method when network availability checks are in the time sources
    /// and Timekeeper can immediately start watching samples.
    pub fn warm_up(&mut self) {
        if self.event_stream.is_none() {
            // Attempt to launch the time source if it is not yet running.
            match self.time_source.launch() {
                Err(err) => {
                    warn!("Error warming up {:?} time source: {:?}", self.role, err);
                    self.record_time_source_failure(TimeSourceError::LaunchFailed);
                }
                Ok(event_stream) => self.event_stream = Some(event_stream),
            }
        }
    }

    /// Returns the `Role` of the time source being managed.
    ///
    /// Note: This method is viable while the `TimeSourceManager` is managing a single time source.
    ///       Once fallback and gating sources are added role will be moved to a property of each
    ///       time sample and this method will be removed.
    #[allow(unused)]
    pub fn role(&self) -> Role {
        self.role
    }

    /// Returns the next valid sample from the time source.
    pub async fn next_sample(&mut self) -> Sample {
        loop {
            // Extract the event stream from self if one exists and attempt to start one if not.
            let mut event_stream = match self.event_stream.take() {
                Some(event_stream) => event_stream,
                None => match self.time_source.launch() {
                    Err(err) => {
                        warn!("Error launching {:?} time source: {:?}", self.role, err);
                        self.record_time_source_failure(TimeSourceError::LaunchFailed);
                        if self.delays_enabled {
                            fasync::Timer::new(fasync::Time::after(RESTART_DELAY)).await;
                        }
                        continue;
                    }
                    Ok(event_stream) => event_stream,
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

    /// Returns the next valid sample from the supplied stream, or an error if the stream
    /// encounters a terminal error. The monotonic provider will be queried exactly once to
    /// validate every `TimeSourceEvent::Sample` received.
    async fn next_sample_from_stream(
        &mut self,
        event_stream: &mut T::EventStream,
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
                TimeSourceEvent::StatusChange { status } => {
                    info!("{:?} changed state to {:?}", self.role, status);
                    self.diagnostics.record(Event::TimeSourceStatus { role: self.role, status });
                    self.last_status = Some(status);
                }
                TimeSourceEvent::Sample(sample) => match self.validate_sample(&sample) {
                    Ok(arrival) => {
                        self.last_accepted_sample_arrival = Some(arrival);
                        return Ok(sample);
                    }
                    Err(error) => {
                        warn!("Rejected invalid sample from {:?}: {:?}", self.role, error);
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

        if self.last_status != Some(Status::Ok) {
            Err(SampleValidationError::StatusNotOk)
        } else if sample.utc < self.backstop {
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

    /// Record a time source failure via diagnostics.
    fn record_time_source_failure(&self, error: TimeSourceError) {
        self.diagnostics.record(Event::TimeSourceFailed { role: self.role, error });
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::diagnostics::FakeDiagnostics,
        crate::enums::{SampleValidationError as SVE, TimeSourceError as TSE},
        crate::time_source::FakeTimeSource,
        anyhow::anyhow,
        fuchsia_async as fasync,
        lazy_static::lazy_static,
    };

    const BACKSTOP_FACTOR: i64 = 100;
    const TEST_ROLE: Role = Role::Monitor;
    const STD_DEV: zx::Duration = zx::Duration::from_millis(22);

    lazy_static! {
        static ref ZERO_TIME: zx::Time = zx::Time::from_nanos(0);
    }

    /// A provider of artificial monotonic times that increment by a fixed duration each call.
    struct FakeMonotonicProvider {
        increment: zx::Duration,
        last_time: zx::Time,
    }

    impl FakeMonotonicProvider {
        /// Constructs a new `FakeMonotonicProvider` that increments by `increment` on each call.
        pub fn new(increment: zx::Duration) -> Self {
            FakeMonotonicProvider { increment, last_time: *ZERO_TIME }
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
        time_source: FakeTimeSource,
        diagnostics: Arc<FakeDiagnostics>,
    ) -> TimeSourceManager<FakeTimeSource, FakeDiagnostics, FakeMonotonicProvider> {
        TimeSourceManager {
            backstop: *ZERO_TIME + (MIN_UPDATE_DELAY * BACKSTOP_FACTOR),
            delays_enabled: true,
            monotonic: FakeMonotonicProvider::new(MIN_UPDATE_DELAY),
            role: TEST_ROLE,
            time_source,
            diagnostics,
            event_stream: None,
            last_status: None,
            last_accepted_sample_arrival: None,
        }
    }

    /// Create a new `TimeSourceManager` using the standard backstop time and role, a monotonic time
    /// that increments by `MIN_UPDATE_DELAY` per sample, and the supplied time source and
    /// diagnostics. Restart and min update delays are disabled.
    fn create_manager_delays_disabled(
        time_source: FakeTimeSource,
        diagnostics: Arc<FakeDiagnostics>,
    ) -> TimeSourceManager<FakeTimeSource, FakeDiagnostics, FakeMonotonicProvider> {
        TimeSourceManager {
            backstop: *ZERO_TIME + (MIN_UPDATE_DELAY * BACKSTOP_FACTOR),
            delays_enabled: false,
            monotonic: FakeMonotonicProvider::new(MIN_UPDATE_DELAY),
            role: TEST_ROLE,
            time_source,
            diagnostics,
            event_stream: None,
            last_status: None,
            last_accepted_sample_arrival: None,
        }
    }

    /// Creates a new time sample from the supplied times. Both UTC and Monotonic are supplied as
    /// a factor to multiply by MIN_UPDATE_DELAY, which is the minimum interval the manager would
    /// accept between samples.rate at hence the rate we choose our fake monotonic clock to tick at.
    fn create_sample(utc_factor: i64, monotonic_factor: i64) -> Sample {
        Sample {
            utc: *ZERO_TIME + (MIN_UPDATE_DELAY * utc_factor),
            monotonic: *ZERO_TIME + (MIN_UPDATE_DELAY * monotonic_factor),
            std_dev: STD_DEV,
        }
    }

    #[test]
    fn role_accessor() {
        let time_source = FakeTimeSource::failing();
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let manager = create_manager(time_source, diagnostics);
        assert_eq!(manager.role(), TEST_ROLE);
    }

    #[fasync::run_until_stalled(test)]
    async fn event_in_future() {
        let time_source = FakeTimeSource::events(vec![
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
        assert_eq!(manager.last_accepted_sample_arrival, Some(*ZERO_TIME + MIN_UPDATE_DELAY * 3));

        diagnostics.assert_events(&[
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
            Event::SampleRejected { role: TEST_ROLE, error: SVE::MonotonicInFuture },
        ]);
    }

    #[fasync::run_until_stalled(test)]
    async fn invalid_status() {
        let time_source = FakeTimeSource::events(vec![
            // Should be ignored since time source is not yet OK.
            TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 1, 1)),
            TimeSourceEvent::StatusChange { status: Status::Network },
            // Should be ignored since time source is not yet OK.
            TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 2, 2)),
            TimeSourceEvent::StatusChange { status: Status::Ok },
            TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 3, 3)),
        ]);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut manager = create_manager(time_source, Arc::clone(&diagnostics));

        assert_eq!(manager.next_sample().await, create_sample(BACKSTOP_FACTOR + 3, 3));

        diagnostics.assert_events(&[
            Event::SampleRejected { role: TEST_ROLE, error: SVE::StatusNotOk },
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Network },
            Event::SampleRejected { role: TEST_ROLE, error: SVE::StatusNotOk },
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
        ]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn restart_on_watch_error() {
        let time_source = FakeTimeSource::result_collections(vec![
            vec![
                Ok(TimeSourceEvent::StatusChange { status: Status::Ok }),
                Ok(TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 1, 1))),
                Err(anyhow!("Walked through wet cement")),
                // Should be ignored since Err caused restart.
                Ok(TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 2, 2))),
            ],
            vec![
                // Should be ignored since time source is not yet OK.
                Ok(TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 3, 2))),
                Ok(TimeSourceEvent::StatusChange { status: Status::Ok }),
                Ok(TimeSourceEvent::from(create_sample(BACKSTOP_FACTOR + 4, 3))),
            ],
        ]);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut manager = create_manager_delays_disabled(time_source, Arc::clone(&diagnostics));

        assert_eq!(manager.next_sample().await, create_sample(BACKSTOP_FACTOR + 1, 1));
        assert_eq!(manager.next_sample().await, create_sample(BACKSTOP_FACTOR + 4, 3));

        diagnostics.assert_events(&[
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
            Event::TimeSourceFailed { role: TEST_ROLE, error: TSE::CallFailed },
            Event::SampleRejected { role: TEST_ROLE, error: SVE::StatusNotOk },
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
        ]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn restart_on_channel_close() {
        let time_source = FakeTimeSource::event_collections(vec![
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

    #[fasync::run_until_stalled(test)]
    async fn warm_up() {
        let time_source = FakeTimeSource::events(vec![
            // State change is valid but shouldn't be recorded since we only warm up.
            TimeSourceEvent::StatusChange { status: Status::Ok },
        ]);
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut manager = create_manager(time_source, Arc::clone(&diagnostics));

        manager.warm_up();

        diagnostics.assert_events(&[]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn restart_on_launch_failure() {
        let time_source = FakeTimeSource::failing();
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let mut manager =
            TimeSourceManager::new(*ZERO_TIME, TEST_ROLE, time_source, Arc::clone(&diagnostics));

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

    #[test]
    fn validate_sample_failures() {
        let mut manager =
            create_manager(FakeTimeSource::failing(), Arc::new(FakeDiagnostics::new()));
        manager.last_status = Some(Status::Ok);

        // The monotonic our manager sees will start at a factor of 1 and increment by 1 each time
        // we try to validate a sample.
        assert_eq!(
            manager.validate_sample(&create_sample(BACKSTOP_FACTOR, 1)),
            Ok(*ZERO_TIME + MIN_UPDATE_DELAY)
        );
        assert_eq!(
            manager.validate_sample(&create_sample(BACKSTOP_FACTOR - 1, 2)),
            Err(SVE::BeforeBackstop)
        );
        assert_eq!(
            manager.validate_sample(&create_sample(BACKSTOP_FACTOR, 0)),
            Err(SVE::MonotonicTooOld)
        );
        assert_eq!(
            manager.validate_sample(&create_sample(BACKSTOP_FACTOR, 100)),
            Err(SVE::MonotonicInFuture)
        );
        // On the next call the monontonic should be a factor of 5, trick the manager into thinking
        // it already accepted an update at 4.5
        manager.last_accepted_sample_arrival =
            Some(zx::Time::from_nanos(MIN_UPDATE_DELAY.into_nanos() / 2 * 9));
        assert_eq!(
            manager.validate_sample(&create_sample(BACKSTOP_FACTOR, 5)),
            Err(SVE::TooCloseToPrevious)
        );
        // But if we disable delays an accepted update of 5.5 at a monotonic of 6 is accepted.
        manager.delays_enabled = false;
        manager.last_accepted_sample_arrival =
            Some(zx::Time::from_nanos(MIN_UPDATE_DELAY.into_nanos() / 2 * 11));
        assert_eq!(
            manager.validate_sample(&create_sample(BACKSTOP_FACTOR, 6)),
            Ok(*ZERO_TIME + MIN_UPDATE_DELAY * 6)
        );
    }
}
