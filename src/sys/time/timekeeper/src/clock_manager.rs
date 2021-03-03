// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics::{Diagnostics, Event},
        enums::{
            ClockCorrectionStrategy, ClockUpdateReason, StartClockSource, Track, WriteRtcOutcome,
        },
        estimator::{error_bound_increase, Estimator},
        rtc::Rtc,
        time_source::TimeSource,
        time_source_manager::{KernelMonotonicProvider, TimeSourceManager},
    },
    chrono::prelude::*,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    log::{error, info},
    std::{
        cmp,
        fmt::{self, Debug},
        sync::Arc,
    },
    time_util::time_at_monotonic,
};

/// One million for PPM calculations
const MILLION: i64 = 1_000_000;

/// The maximum clock frequency adjustment that Timekeeper will apply to slew away a UTC error,
/// expressed as a PPM deviation from nominal.
const MAX_RATE_CORRECTION_PPM: i64 = 200;

/// The clock frequency adjustment that Timekeeper will prefer when slewing away a UTC error,
/// expressed as a PPM deviation from nominal.
const NOMINAL_RATE_CORRECTION_PPM: i64 = 20;

/// The longest duration for which Timekeeper will apply a clock frequency adjustment in response to
/// a single time update.
const MAX_SLEW_DURATION: zx::Duration = zx::Duration::from_minutes(90);

/// The largest error that may be corrected through slewing at the maximum rate.
const MAX_RATE_MAX_ERROR: zx::Duration =
    zx::Duration::from_nanos((MAX_SLEW_DURATION.into_nanos() * MAX_RATE_CORRECTION_PPM) / MILLION);

/// The largest error that may be corrected through slewing at the preferred rate.
const NOMINAL_RATE_MAX_ERROR: zx::Duration = zx::Duration::from_nanos(
    (MAX_SLEW_DURATION.into_nanos() * NOMINAL_RATE_CORRECTION_PPM) / MILLION,
);

/// The change in error bound that requires an update to the reported value. In some conditions
/// error bound may be updated even when it has changed by less than this value.
const ERROR_BOUND_UPDATE: u64 = 100_000_000; // 100ms

/// The interval at which the error bound will be refreshed while no other events are in progress.
const ERROR_REFRESH_INTERVAL: zx::Duration = zx::Duration::from_minutes(6);

/// Describes how a clock will be slewed in order to correct time.
#[derive(PartialEq)]
struct Slew {
    /// Clock adjustment in parts per million.
    rate_adjust: i32,
    /// Duration for which the slew is to maintained.
    duration: zx::Duration,
}

impl Slew {
    /// Returns the total correction achieved by the slew.
    fn correction(&self) -> zx::Duration {
        zx::Duration::from_nanos((self.duration.into_nanos() * self.rate_adjust as i64) / MILLION)
    }
}

impl Default for Slew {
    fn default() -> Slew {
        Slew { rate_adjust: 0, duration: zx::Duration::from_nanos(0) }
    }
}

impl Debug for Slew {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Slew")
            .field("rate_ppm", &self.rate_adjust)
            .field("duration_ms", &self.duration.into_millis())
            .finish()
    }
}

/// Generates and applies all updates needed to maintain a userspace clock object (and optionally
/// also a real time clock) with accurate UTC time. New time samples are received from a
/// `TimeSourceManager` and a UTC estimate is produced based on these samples by an `Estimator`.
pub struct ClockManager<T: TimeSource, R: Rtc, D: Diagnostics> {
    /// The userspace clock to be maintained.
    clock: Arc<zx::Clock>,
    /// The `TimeSourceManager` that supplies validated samples from a time source.
    time_source_manager: TimeSourceManager<T, D, KernelMonotonicProvider>,
    /// The `Estimator` that maintains an estimate of the UTC and frequency, populated after the
    /// first sample has been received.
    estimator: Option<Estimator<D>>,
    /// An optional real time clock that will be updated when new UTC estimates are produced.
    rtc: Option<R>,
    /// A diagnostics implementation for recording events of note.
    diagnostics: Arc<D>,
    /// The track of the estimate being managed.
    track: Track,
    /// A task used to complete any delayed clock updates, such as finishing a slew or increasing
    /// error bound in the absence of corrections.
    delayed_updates: Option<fasync::Task<()>>,
}

impl<T: TimeSource, R: Rtc, D: 'static + Diagnostics> ClockManager<T, R, D> {
    /// Construct a new `ClockManager` and start synchronizing the clock. The returned future
    /// will never complete.
    pub async fn execute(
        clock: Arc<zx::Clock>,
        time_source_manager: TimeSourceManager<T, D, KernelMonotonicProvider>,
        rtc: Option<R>,
        diagnostics: Arc<D>,
        track: Track,
    ) {
        ClockManager::new(clock, time_source_manager, rtc, diagnostics, track)
            .maintain_clock()
            .await
    }

    // TODO(jsankey): Once the network availability detection code is in the time sources (so we
    // no longer need to wait for network in between initializing the clock from rtc and
    // communicating with a time source) add an `execute_from_rtc` method that populates the clock
    // with the rtc value before beginning the maintain clock method.

    /// Construct a new `ClockManager`.
    fn new(
        clock: Arc<zx::Clock>,
        time_source_manager: TimeSourceManager<T, D, KernelMonotonicProvider>,
        rtc: Option<R>,
        diagnostics: Arc<D>,
        track: Track,
    ) -> Self {
        ClockManager {
            clock,
            time_source_manager,
            estimator: None,
            rtc,
            diagnostics,
            track,
            delayed_updates: None,
        }
    }

    /// Maintain the clock indefinitely. This future will never complete.
    async fn maintain_clock(mut self) {
        let details = self.clock.get_details().expect("failed to get UTC clock details");
        let mut clock_started =
            details.backstop.into_nanos() != details.ticks_to_synthetic.synthetic_offset;
        std::mem::drop(details);

        loop {
            // Acquire a new sample.
            let sample = self.time_source_manager.next_sample().await;

            // Feed it to the estimator (or initialize the estimator).
            match &mut self.estimator {
                Some(estimator) => estimator.update(sample),
                None => {
                    self.estimator =
                        Some(Estimator::new(self.track, sample, Arc::clone(&self.diagnostics)))
                }
            }
            // Note: Both branches of the match led to a populated estimator so safe to unwrap.
            let estimator: &mut Estimator<D> = &mut self.estimator.as_mut().unwrap();

            // Determine the intended (UTC - monotonic offset) and start or correct the clock.
            let reference_mono = zx::Time::get_monotonic();
            let estimate_utc = estimator.estimate(reference_mono);
            let estimate_offset = estimate_utc - reference_mono;
            if !clock_started {
                self.start_clock(estimate_offset);
                clock_started = true;
            } else {
                self.apply_clock_correction(estimate_offset).await;
            }

            // Update the RTC clock if we have one.
            // Note this only applies to primary so we don't include the track in our log messages.
            if let Some(ref rtc) = self.rtc {
                let utc_chrono = Utc.timestamp_nanos(estimate_utc.into_nanos());
                let outcome = match rtc.set(estimate_utc).await {
                    Err(err) => {
                        error!("failed to update RTC and ZX_CLOCK_UTC to {}: {}", utc_chrono, err);
                        WriteRtcOutcome::Failed
                    }
                    Ok(()) => {
                        info!("updated RTC to {}", utc_chrono);
                        WriteRtcOutcome::Succeeded
                    }
                };
                self.diagnostics.record(Event::WriteRtc { outcome });
            }
        }
    }

    /// Starts the clock at the requested offset between utc and monotonic time, taking error bound
    /// from the estimator and recording diagnostic events.
    fn start_clock(&mut self, new_offset: zx::Duration) {
        let mono = zx::Time::get_monotonic();
        let utc = mono + new_offset;
        let utc_chrono = Utc.timestamp_nanos(utc.into_nanos());
        let error_bound =
            self.estimator.as_ref().expect("Estimator not initialized").error_bound(mono);
        self.update_clock(zx::ClockUpdate::new().value(utc).error_bounds(error_bound));
        self.diagnostics.record(Event::StartClock {
            track: self.track,
            source: StartClockSource::External(self.time_source_manager.role()),
        });
        info!("started {:?} clock from external source at {}", self.track, utc_chrono);
        self.set_delayed_update_task(vec![]);
    }

    /// Applies a correction to the clock to reach the requested offset between utc and monotonic
    /// time, taking error bound from the estimator, selecting and applying the most appropriate
    /// strategy, and recording diagnostic events.
    async fn apply_clock_correction(&mut self, new_offset: zx::Duration) {
        let correction = new_offset - get_clock_offset(&self.clock);
        let track = self.track;
        let estimator = self.estimator.as_ref().expect("Estimator not initialized");

        let (strategy, slew) = determine_strategy(correction);
        self.diagnostics.record(Event::ClockCorrection { track, correction, strategy });

        match strategy {
            ClockCorrectionStrategy::NominalRateSlew | ClockCorrectionStrategy::MaxDurationSlew => {
                // Any pending clock updates will be superseded by the handling of this one.
                if let Some(task) = self.delayed_updates.take() {
                    task.cancel().await;
                };

                let mut updates =
                    clock_updates_for_slew(&slew, zx::Time::get_monotonic(), &estimator);

                // The first update is guaranteed to be immediate.
                let (_, update, reason) = updates.remove(0);
                self.update_clock(update);
                self.record_clock_update(reason);
                info!("started {:?} {:?} with {} scheduled updates", track, slew, updates.len());

                // Create a task to asynchronously apply all the remaining updates and then increase
                // error bound over time.
                self.set_delayed_update_task(updates);
            }
            ClockCorrectionStrategy::Step => {
                // Any pending clock updates should be superseded by this time step.
                if let Some(task) = self.delayed_updates.take() {
                    task.cancel().await;
                };

                let mono = zx::Time::get_monotonic();
                let utc = mono + new_offset;
                let error_bound = estimator.error_bound(mono);
                self.update_clock(
                    zx::ClockUpdate::new().value(utc).error_bounds(error_bound).rate_adjust(0),
                );
                self.record_clock_update(ClockUpdateReason::TimeStep);
                let utc_chrono = Utc.timestamp_nanos(utc.into_nanos());
                info!("stepped {:?} clock to {}", self.track, utc_chrono);
                self.set_delayed_update_task(vec![]);
            }
        }
    }

    /// Sets a task that asynchronously applies the supplied scheduled clock updates and then
    /// periodically applies an increase in the error bound indefinitely.
    fn set_delayed_update_task(
        &mut self,
        scheduled_updates: Vec<(fasync::Time, zx::ClockUpdate, ClockUpdateReason)>,
    ) {
        let clock = Arc::clone(&self.clock);
        let diagnostics = Arc::clone(&self.diagnostics);
        let track = self.track;

        let async_now = fasync::Time::now();
        // The first periodic step in error bound occurs a fixed duration after the last
        // scheduled update or after current time if no scheduled updates were supplied.
        let mut step_async_time =
            scheduled_updates.last().map(|tup| tup.0).unwrap_or(async_now) + ERROR_REFRESH_INTERVAL;
        // Updates are supplied in fuchsia_async time for ease of scheduling and unit testing, but
        // we need to calculate a corresponding monotonic time to read the absolute error bound.
        let step_mono_time = zx::Time::get_monotonic() + (step_async_time - async_now);
        let mut step_error_bound =
            self.estimator.as_ref().expect("Estimator not initialized").error_bound(step_mono_time);

        self.delayed_updates = Some(fasync::Task::spawn(async move {
            for (update_time, update, reason) in scheduled_updates.into_iter() {
                fasync::Timer::new(update_time).await;
                info!("executing scheduled {:?} clock update: {:?}, {:?}", track, reason, update);
                update_clock(&clock, &track, update);
                diagnostics.record(Event::UpdateClock { track, reason });
            }

            loop {
                fasync::Timer::new(step_async_time).await;
                info!("increasing {:?} error bound to {:?}ns", track, step_error_bound);
                update_clock(&clock, &track, zx::ClockUpdate::new().error_bounds(step_error_bound));
                diagnostics
                    .record(Event::UpdateClock { track, reason: ClockUpdateReason::IncreaseError });
                step_async_time += ERROR_REFRESH_INTERVAL;
                step_error_bound += error_bound_increase(ERROR_REFRESH_INTERVAL);
            }
        }));
    }

    /// Applies an update to the clock.
    fn update_clock(&mut self, update: zx::ClockUpdate) {
        update_clock(&self.clock, &self.track, update);
    }

    /// Records the reason for a clock update with diagnostics.
    fn record_clock_update(&self, reason: ClockUpdateReason) {
        self.diagnostics.record(Event::UpdateClock { track: self.track, reason });
    }
}

/// Applies an update to the supplied clock, panicking with a comprehensible error on failure.
fn update_clock(clock: &Arc<zx::Clock>, track: &Track, update: zx::ClockUpdate) {
    if let Err(status) = clock.update(update) {
        // Clock update errors should only be caused by an invalid clock (or potentially a
        // serious bug in the generation of a time update). There isn't anything Timekeeper
        // could do to gracefully handle them.
        panic!("Failed to apply update to {:?} clock: {}", track, status);
    }
}

/// Determines the strategy that should be used to apply the supplied correction and instructions
/// for performing a slew. `Slew` will be set to default in the strategies that do not require
/// slewing.
fn determine_strategy(correction: zx::Duration) -> (ClockCorrectionStrategy, Slew) {
    let correction_nanos = correction.into_nanos();
    let correction_abs = zx::Duration::from_nanos(correction_nanos.abs());
    let sign = if correction_nanos < 0 { -1 } else { 1 };

    if correction_abs < NOMINAL_RATE_MAX_ERROR {
        let rate_adjust = NOMINAL_RATE_CORRECTION_PPM * sign;
        let duration = (correction_abs * MILLION) / NOMINAL_RATE_CORRECTION_PPM;
        (
            ClockCorrectionStrategy::NominalRateSlew,
            Slew { rate_adjust: rate_adjust as i32, duration },
        )
    } else if correction_abs < MAX_RATE_MAX_ERROR {
        // Round rate up to the next greater PPM such that duration will be slightly under
        // MAX_SLEW_DURATION rather that slightly over MAX_SLEW_DURATION.
        let rate_adjust = ((correction_nanos * MILLION) / MAX_SLEW_DURATION.into_nanos()) + sign;
        let duration = (correction * MILLION) / rate_adjust;
        (
            ClockCorrectionStrategy::MaxDurationSlew,
            Slew { rate_adjust: rate_adjust as i32, duration },
        )
    } else {
        (ClockCorrectionStrategy::Step, Slew::default())
    }
}

/// Returns the offset between UTC and monotonic times in the supplied clock.
fn get_clock_offset(clock: &zx::Clock) -> zx::Duration {
    let monotonic_ref = zx::Time::get_monotonic();
    time_at_monotonic(&clock, monotonic_ref) - monotonic_ref
}

/// Returns a vector of (async::Time, zx::ClockUpdate, ClockUpdateReason) tuples describing the
/// updates to make to a clock during the supplied slew, calculating error bounds using the
/// supplied estimator. The first update is guaranteed to be requested immediately.
fn clock_updates_for_slew<D: Diagnostics>(
    slew: &Slew,
    monotonic: zx::Time,
    estimator: &Estimator<D>,
) -> Vec<(fasync::Time, zx::ClockUpdate, ClockUpdateReason)> {
    // Note: fuchsia_async time can be mocked independently so can't assume its equivalent to
    // the supplied monotonic time.
    let start_time = fasync::Time::now();
    let finish_time = start_time + slew.duration;

    // The initial error bound is the estimate error bound plus the entire correction.
    let initial_error_bound =
        estimator.error_bound(monotonic) + slew.correction().into_nanos().abs() as u64;
    // The final error bound is the estimate error bound when we finish the slew.
    let final_error_bound = estimator.error_bound(monotonic + slew.duration);

    // For large slews we expect the reduction in error bound while applying the correction to
    // exceed the growth in error bound due to oscillator error but there is no guarantee of
    // this. If error bound will increase through the slew, just use the worst case throughout.
    let begin_error_bound = cmp::max(initial_error_bound, final_error_bound);

    // The final vector is composed of an initial update to start the slew...
    let mut updates = vec![(
        start_time,
        zx::ClockUpdate::new().rate_adjust(slew.rate_adjust).error_bounds(begin_error_bound),
        ClockUpdateReason::BeginSlew,
    )];

    // ... intermediate updates to reduce the error bound if it reduces by more than the
    // threshold during the course of the slew ...
    if initial_error_bound > final_error_bound + ERROR_BOUND_UPDATE {
        let bound_change = (initial_error_bound - final_error_bound) as i64;
        let error_update_interval = zx::Duration::from_nanos(
            ((slew.duration.into_nanos() as i128 * ERROR_BOUND_UPDATE as i128)
                / bound_change as i128) as i64,
        );
        let mut i: i64 = 1;
        while start_time + error_update_interval * i < finish_time {
            updates.push((
                start_time + error_update_interval * i,
                zx::ClockUpdate::new()
                    .error_bounds(initial_error_bound - ERROR_BOUND_UPDATE * i as u64),
                ClockUpdateReason::ReduceError,
            ));
            i += 1;
        }
    }

    // ... and a final update to return the rate to normal.
    updates.push((
        finish_time,
        zx::ClockUpdate::new().rate_adjust(0).error_bounds(final_error_bound),
        ClockUpdateReason::EndSlew,
    ));
    updates
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            diagnostics::{FakeDiagnostics, ANY_DURATION},
            enums::{Role, WriteRtcOutcome},
            rtc::FakeRtc,
            time_source::{Event as TimeSourceEvent, FakeTimeSource, Sample},
        },
        fidl_fuchsia_time_external::{self as ftexternal, Status},
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::FutureExt,
        lazy_static::lazy_static,
        test_util::{assert_geq, assert_gt, assert_leq, assert_lt},
        zx::DurationNum,
    };

    const NANOS_PER_SECOND: i64 = 1_000_000_000;

    const TEST_ROLE: Role = Role::Primary;

    const SAMPLE_SPACING: zx::Duration = zx::Duration::from_millis(100);
    const OFFSET: zx::Duration = zx::Duration::from_seconds(1111_000);
    const OFFSET_2: zx::Duration = zx::Duration::from_seconds(2222_000);
    const STD_DEV: zx::Duration = zx::Duration::from_millis(88);
    const BACKSTOP_TIME: zx::Time = zx::Time::from_nanos(222222 * NANOS_PER_SECOND);

    lazy_static! {
        static ref TEST_TRACK: Track = Track::from(TEST_ROLE);
        static ref CLOCK_OPTS: zx::ClockOpts = zx::ClockOpts::empty();
        static ref START_CLOCK_SOURCE: StartClockSource = StartClockSource::External(TEST_ROLE);
    }

    /// Creates and starts a new clock with default options.
    fn create_clock() -> Arc<zx::Clock> {
        let clock = zx::Clock::create(*CLOCK_OPTS, Some(BACKSTOP_TIME)).unwrap();
        clock.update(zx::ClockUpdate::new().value(BACKSTOP_TIME)).unwrap();
        Arc::new(clock)
    }

    /// Creates a new `ClockManager` from a time source manager that outputs the supplied samples.
    fn create_clock_manager(
        clock: Arc<zx::Clock>,
        samples: Vec<Sample>,
        final_time_source_status: Option<ftexternal::Status>,
        rtc: Option<FakeRtc>,
        diagnostics: Arc<FakeDiagnostics>,
    ) -> ClockManager<FakeTimeSource, FakeRtc, FakeDiagnostics> {
        let mut events: Vec<TimeSourceEvent> =
            samples.into_iter().map(|sample| TimeSourceEvent::from(sample)).collect();
        events.insert(0, TimeSourceEvent::StatusChange { status: ftexternal::Status::Ok });
        if let Some(status) = final_time_source_status {
            events.push(TimeSourceEvent::StatusChange { status });
        }
        let time_source = FakeTimeSource::events(events);
        let time_source_manager = TimeSourceManager::new_with_delays_disabled(
            BACKSTOP_TIME,
            TEST_ROLE,
            time_source,
            Arc::clone(&diagnostics),
        );
        ClockManager::new(clock, time_source_manager, rtc, diagnostics, *TEST_TRACK)
    }

    #[fuchsia::test]
    fn determine_strategy_fn() {
        for sign in vec![-1, 1] {
            let (strategy, slew) = determine_strategy((sign * 5).micros());
            assert_eq!(strategy, ClockCorrectionStrategy::NominalRateSlew);
            assert_eq!(slew, Slew { rate_adjust: (sign * 20) as i32, duration: 250.millis() });

            let (strategy, slew) = determine_strategy((sign * 5).millis());
            assert_eq!(strategy, ClockCorrectionStrategy::NominalRateSlew);
            assert_eq!(slew, Slew { rate_adjust: (sign * 20) as i32, duration: 250.seconds() });
            assert_eq!(slew.correction(), (sign * 5).millis());

            let (strategy, slew) = determine_strategy((sign * 500).millis());
            assert_eq!(strategy, ClockCorrectionStrategy::MaxDurationSlew);
            assert_eq!(
                slew,
                Slew { rate_adjust: (sign * 93) as i32, duration: 5376344086021.nanos() }
            );

            let (strategy, slew) = determine_strategy((sign * 2).seconds());
            assert_eq!(strategy, ClockCorrectionStrategy::Step);
            assert_eq!(slew, Slew::default());
        }
    }

    #[fuchsia::test]
    fn clock_updates_for_slew_fn() {
        let executor = fasync::Executor::new_with_fake_time().unwrap();
        executor.set_fake_time(fasync::Time::from_nanos(0));

        // Manually create a minimal estimator.
        let monotonic_ref = zx::Time::get_monotonic();
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let estimator = Estimator::new(
            *TEST_TRACK,
            Sample::new(monotonic_ref + OFFSET, monotonic_ref, STD_DEV),
            Arc::clone(&diagnostics),
        );

        // Simple constructor lambdas to improve readability of the test logic.
        let full_update = |rate: i32, error_bound: u64| -> zx::ClockUpdate {
            zx::ClockUpdate::new().rate_adjust(rate).error_bounds(error_bound)
        };
        let error_update = |error_bound: u64| -> zx::ClockUpdate {
            zx::ClockUpdate::new().error_bounds(error_bound)
        };
        let time_seconds =
            |seconds: i64| -> fasync::Time { fasync::Time::from_nanos(seconds * NANOS_PER_SECOND) };

        // A short slew should contain no error bound updates.
        let slew = Slew { rate_adjust: -50, duration: 10.seconds() };
        let estimate_bound_at_ref = estimator.error_bound(monotonic_ref);
        let bound_at_start = estimate_bound_at_ref + slew.correction().into_nanos().abs() as u64;
        let bound_at_end = estimator.error_bound(monotonic_ref + slew.duration);
        assert_eq!(
            clock_updates_for_slew(&slew, monotonic_ref, &estimator),
            vec![
                (
                    time_seconds(0),
                    full_update(slew.rate_adjust, bound_at_start),
                    ClockUpdateReason::BeginSlew
                ),
                (
                    time_seconds(slew.duration.into_seconds()),
                    full_update(0, bound_at_end),
                    ClockUpdateReason::EndSlew
                ),
            ]
        );

        // A larger slew should contain as many error bound reductions as needed.
        let slew = Slew { rate_adjust: 100, duration: 1.hour() };
        let bound_at_start = estimate_bound_at_ref + slew.correction().into_nanos().abs() as u64;
        let bound_at_end = estimator.error_bound(monotonic_ref + slew.duration);
        let update_interval_nanos = ((slew.duration.into_nanos() as i128
            * ERROR_BOUND_UPDATE as i128)
            / (bound_at_start - bound_at_end) as i128) as i64;
        assert_eq!(
            clock_updates_for_slew(&slew, monotonic_ref, &estimator),
            vec![
                (
                    time_seconds(0),
                    full_update(slew.rate_adjust, bound_at_start),
                    ClockUpdateReason::BeginSlew
                ),
                (
                    fasync::Time::from_nanos(update_interval_nanos),
                    error_update(bound_at_start - ERROR_BOUND_UPDATE),
                    ClockUpdateReason::ReduceError,
                ),
                (
                    fasync::Time::from_nanos(2 * update_interval_nanos),
                    error_update(bound_at_start - 2 * ERROR_BOUND_UPDATE),
                    ClockUpdateReason::ReduceError,
                ),
                (
                    time_seconds(slew.duration.into_seconds()),
                    full_update(0, bound_at_end),
                    ClockUpdateReason::EndSlew
                ),
            ]
        );

        // When the error reduction from applying the correction is smaller than the growth from the
        // oscillator uncertainty the error bound should be fixed at the final value with no
        // intermediate updates.
        let slew = Slew { rate_adjust: 1, duration: 10.hours() };
        let bound_at_end = estimator.error_bound(monotonic_ref + slew.duration);
        assert_eq!(
            clock_updates_for_slew(&slew, monotonic_ref, &estimator,),
            vec![
                (time_seconds(0), full_update(1, bound_at_end), ClockUpdateReason::BeginSlew),
                (
                    time_seconds(slew.duration.into_seconds()),
                    full_update(0, bound_at_end),
                    ClockUpdateReason::EndSlew
                ),
            ]
        );
    }

    #[fuchsia::test]
    fn single_update_with_rtc() {
        let mut executor = fasync::Executor::new().unwrap();

        let clock = create_clock();
        let rtc = FakeRtc::valid(BACKSTOP_TIME);
        let diagnostics = Arc::new(FakeDiagnostics::new());

        // Create a clock manager.
        let monotonic_ref = zx::Time::get_monotonic();
        let clock_manager = create_clock_manager(
            Arc::clone(&clock),
            vec![Sample::new(monotonic_ref + OFFSET, monotonic_ref, STD_DEV)],
            None,
            Some(rtc.clone()),
            Arc::clone(&diagnostics),
        );

        // Maintain the clock until no more work remains.
        let monotonic_before = zx::Time::get_monotonic();
        let mut fut = clock_manager.maintain_clock().boxed();
        let _ = executor.run_until_stalled(&mut fut);
        let updated_utc = clock.read().unwrap();
        let monotonic_after = zx::Time::get_monotonic();

        // Check that the clocks have been updated. The UTC should be bounded by the offset we
        // supplied added to the monotonic window in which the calculation took place.
        assert_geq!(updated_utc, monotonic_before + OFFSET);
        assert_leq!(updated_utc, monotonic_after + OFFSET);
        assert_geq!(rtc.last_set().unwrap(), monotonic_before + OFFSET);
        assert_leq!(rtc.last_set().unwrap(), monotonic_after + OFFSET);

        // Check that the correct diagnostic events were logged.
        diagnostics.assert_events(&[
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
            Event::EstimateUpdated { track: *TEST_TRACK, offset: OFFSET, sqrt_covariance: STD_DEV },
            Event::StartClock { track: *TEST_TRACK, source: *START_CLOCK_SOURCE },
            Event::WriteRtc { outcome: WriteRtcOutcome::Succeeded },
        ]);
    }

    #[fuchsia::test]
    fn single_update_without_rtc() {
        let mut executor = fasync::Executor::new().unwrap();

        let clock = create_clock();
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let monotonic_ref = zx::Time::get_monotonic();
        let clock_manager = create_clock_manager(
            Arc::clone(&clock),
            vec![Sample::new(monotonic_ref + OFFSET, monotonic_ref, STD_DEV)],
            None,
            None,
            Arc::clone(&diagnostics),
        );

        // Maintain the clock until no more work remains
        let monotonic_before = zx::Time::get_monotonic();
        let mut fut = clock_manager.maintain_clock().boxed();
        let _ = executor.run_until_stalled(&mut fut);
        let updated_utc = clock.read().unwrap();
        let monotonic_after = zx::Time::get_monotonic();

        // Check that the clock has been updated. The UTC should be bounded by the offset we
        // supplied added to the monotonic window in which the calculation took place.
        assert_geq!(updated_utc, monotonic_before + OFFSET);
        assert_leq!(updated_utc, monotonic_after + OFFSET);

        // If we keep waiting the error bound should increase in the absence of updates.
        let details1 = clock.get_details().unwrap();
        assert!(executor.wake_next_timer().is_some());
        let _ = executor.run_until_stalled(&mut fut);
        let details2 = clock.get_details().unwrap();
        assert_eq!(details2.mono_to_synthetic, details1.mono_to_synthetic);
        assert_gt!(details2.error_bounds, details1.error_bounds);
        assert!(executor.wake_next_timer().is_some());
        let _ = executor.run_until_stalled(&mut fut);
        let details3 = clock.get_details().unwrap();
        assert_eq!(details3.mono_to_synthetic, details1.mono_to_synthetic);
        assert_gt!(details3.error_bounds, details2.error_bounds);

        // Check that the correct diagnostic events were logged.
        diagnostics.assert_events(&[
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
            Event::EstimateUpdated { track: *TEST_TRACK, offset: OFFSET, sqrt_covariance: STD_DEV },
            Event::StartClock { track: *TEST_TRACK, source: *START_CLOCK_SOURCE },
            Event::UpdateClock { track: *TEST_TRACK, reason: ClockUpdateReason::IncreaseError },
            Event::UpdateClock { track: *TEST_TRACK, reason: ClockUpdateReason::IncreaseError },
        ]);
    }

    #[fuchsia::test]
    fn subsequent_updates_accepted() {
        let mut executor = fasync::Executor::new().unwrap();

        let clock = create_clock();
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let monotonic_ref = zx::Time::get_monotonic();
        let clock_manager = create_clock_manager(
            Arc::clone(&clock),
            vec![
                Sample::new(
                    monotonic_ref - SAMPLE_SPACING + OFFSET,
                    monotonic_ref - SAMPLE_SPACING,
                    STD_DEV,
                ),
                Sample::new(monotonic_ref + OFFSET_2, monotonic_ref, STD_DEV),
            ],
            None,
            None,
            Arc::clone(&diagnostics),
        );

        // Maintain the clock until no more work remains
        let monotonic_before = zx::Time::get_monotonic();
        let mut fut = clock_manager.maintain_clock().boxed();
        let _ = executor.run_until_stalled(&mut fut);
        let updated_utc = clock.read().unwrap();
        let monotonic_after = zx::Time::get_monotonic();

        // Since we used the same covariance for the first two samples the offset in the Kalman
        // filter is roughly midway between the sample offsets, but slight closer to the second
        // because oscillator uncertainty.
        let expected_offset = 1666500000080699.nanos();

        // Check that the clock has been updated. The UTC should be bounded by the expected offset
        // added to the monotonic window in which the calculation took place.
        assert_geq!(updated_utc, monotonic_before + expected_offset);
        assert_leq!(updated_utc, monotonic_after + expected_offset);

        // Check that the correct diagnostic events were logged.
        diagnostics.assert_events(&[
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
            Event::EstimateUpdated { track: *TEST_TRACK, offset: OFFSET, sqrt_covariance: STD_DEV },
            Event::StartClock { track: *TEST_TRACK, source: *START_CLOCK_SOURCE },
            Event::EstimateUpdated {
                track: *TEST_TRACK,
                offset: expected_offset,
                sqrt_covariance: 62225396.nanos(),
            },
            Event::ClockCorrection {
                track: *TEST_TRACK,
                correction: ANY_DURATION,
                strategy: ClockCorrectionStrategy::Step,
            },
            Event::UpdateClock { track: *TEST_TRACK, reason: ClockUpdateReason::TimeStep },
        ]);
    }

    #[fuchsia::test]
    fn correction_by_slew() {
        let mut executor = fasync::Executor::new().unwrap();

        // Calculate a small change in offset that will be corrected by slewing and is large enough
        // to require an error bound reduction. Note the tests doesn't have to actually wait this
        // long since we can manually trigger async timers.
        let delta_offset = 600.millis();
        let filtered_delta_offset = delta_offset / 2;

        let clock = create_clock();
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let monotonic_ref = zx::Time::get_monotonic();
        let clock_manager = create_clock_manager(
            Arc::clone(&clock),
            vec![
                Sample::new(
                    monotonic_ref - SAMPLE_SPACING + OFFSET,
                    monotonic_ref - SAMPLE_SPACING,
                    STD_DEV,
                ),
                Sample::new(monotonic_ref + OFFSET + delta_offset, monotonic_ref, STD_DEV),
            ],
            // Leave the time source in network unavailable after its sent the samples so it
            // doesn't get killed for being unresponsive while the slew is applied.
            Some(ftexternal::Status::Network),
            None,
            Arc::clone(&diagnostics),
        );

        // Maintain the clock until no more work remains, which should correspond to having started
        // a clock skew but blocking on the timer to end it.
        let monotonic_before = zx::Time::get_monotonic();
        let mut fut = clock_manager.maintain_clock().boxed();
        let _ = executor.run_until_stalled(&mut fut);
        let updated_utc = clock.read().unwrap();
        let details = clock.get_details().unwrap();
        let monotonic_after = zx::Time::get_monotonic();

        // The clock time should still be very close to the original value but the details should
        // show that a rate change is in progress.
        assert_geq!(updated_utc, monotonic_before + OFFSET);
        assert_leq!(updated_utc, monotonic_after + OFFSET + filtered_delta_offset);
        assert_geq!(details.mono_to_synthetic.rate.synthetic_ticks, 1000050);
        assert_eq!(details.mono_to_synthetic.rate.reference_ticks, 1000000);
        assert_geq!(details.last_rate_adjust_update_ticks, details.last_value_update_ticks);

        // After waiting for the first deferred update the clock should still be still at the
        // modified rate with a smaller error bound.
        assert!(executor.wake_next_timer().is_some());
        let _ = executor.run_until_stalled(&mut fut);
        let details2 = clock.get_details().unwrap();
        assert_geq!(details2.mono_to_synthetic.rate.synthetic_ticks, 1000050);
        assert_eq!(details2.mono_to_synthetic.rate.reference_ticks, 1000000);
        assert_lt!(details2.error_bounds, details.error_bounds);

        // After waiting for the next deferred update the clock should be back to the original rate
        // with an even smaller error bound.
        assert!(executor.wake_next_timer().is_some());
        let _ = executor.run_until_stalled(&mut fut);
        let details3 = clock.get_details().unwrap();
        assert_eq!(details3.mono_to_synthetic.rate.synthetic_ticks, 1000000);
        assert_eq!(details3.mono_to_synthetic.rate.reference_ticks, 1000000);
        assert_lt!(details3.error_bounds, details2.error_bounds);

        // If we keep on waiting the error bound should keep increasing in the absence of updates.
        assert!(executor.wake_next_timer().is_some());
        let _ = executor.run_until_stalled(&mut fut);
        let details4 = clock.get_details().unwrap();
        assert_eq!(details4.mono_to_synthetic, details3.mono_to_synthetic);
        assert_gt!(details4.error_bounds, details3.error_bounds);
        assert!(executor.wake_next_timer().is_some());
        let _ = executor.run_until_stalled(&mut fut);
        let details5 = clock.get_details().unwrap();
        assert_eq!(details5.mono_to_synthetic, details3.mono_to_synthetic);
        assert_gt!(details5.error_bounds, details4.error_bounds);

        // Check that the correct diagnostic events were logged.
        diagnostics.assert_events(&[
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
            Event::EstimateUpdated { track: *TEST_TRACK, offset: OFFSET, sqrt_covariance: STD_DEV },
            Event::StartClock { track: *TEST_TRACK, source: *START_CLOCK_SOURCE },
            Event::EstimateUpdated {
                track: *TEST_TRACK,
                offset: OFFSET + filtered_delta_offset,
                sqrt_covariance: 62225396.nanos(),
            },
            Event::ClockCorrection {
                track: *TEST_TRACK,
                correction: ANY_DURATION,
                strategy: ClockCorrectionStrategy::MaxDurationSlew,
            },
            Event::UpdateClock { track: *TEST_TRACK, reason: ClockUpdateReason::BeginSlew },
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Network },
            Event::UpdateClock { track: *TEST_TRACK, reason: ClockUpdateReason::ReduceError },
            Event::UpdateClock { track: *TEST_TRACK, reason: ClockUpdateReason::EndSlew },
            Event::UpdateClock { track: *TEST_TRACK, reason: ClockUpdateReason::IncreaseError },
            Event::UpdateClock { track: *TEST_TRACK, reason: ClockUpdateReason::IncreaseError },
        ]);
    }
}
