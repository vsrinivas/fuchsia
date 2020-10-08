// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics::{Diagnostics, Event},
        enums::{StartClockSource, Track, WriteRtcOutcome},
        estimator::Estimator,
        notifier::Notifier,
        rtc::Rtc,
        time_source::TimeSource,
        time_source_manager::{KernelMonotonicProvider, TimeSourceManager},
    },
    chrono::prelude::*,
    fidl_fuchsia_time as ftime, fuchsia_zircon as zx,
    log::{error, info},
    std::sync::Arc,
};

/// The minimum size UTC mismatch that will lead to a clock update. If the difference between the
/// current clock value and new estimate is smaller than this no update will be applied.
const CLOCK_UPDATE_THRESHOLD: zx::Duration = zx::Duration::from_millis(1);

/// Generates and applies all updates needed to maintain a userspace clock object (and optionally
/// also a real time clock) with accurate UTC time. New time samples are received from a
/// `TimeSourceManager` and a UTC estimate is produced based on these samples by an `Estimator`.
pub struct ClockManager<T: TimeSource, R: Rtc, D: Diagnostics> {
    /// The userspace clock to be maintained.
    clock: zx::Clock,
    /// The `TimeSourceManager` that supplies validated samples from a time source.
    time_source_manager: TimeSourceManager<T, D, KernelMonotonicProvider>,
    /// The `Estimator` that maintains an estimate of the UTC and frequency, populated after the
    /// first sample has been received.
    estimator: Option<Estimator>,
    /// An optional real time clock that will be updated when new UTC estimates are produced.
    rtc: Option<R>,
    /// An optional notifier used to communicate changes in the clock synchronization state.
    notifier: Option<Notifier>,
    /// A diagnostics implementation for recording events of note.
    diagnostics: Arc<D>,
    /// The track of the estimate being managed.
    track: Track,
}

impl<T: TimeSource, R: Rtc, D: Diagnostics> ClockManager<T, R, D> {
    /// Construct a new `ClockManager` and start synchronizing the clock. The returned future
    /// will never complete.
    pub async fn execute(
        clock: zx::Clock,
        time_source_manager: TimeSourceManager<T, D, KernelMonotonicProvider>,
        rtc: Option<R>,
        notifier: Option<Notifier>,
        diagnostics: Arc<D>,
        track: Track,
    ) {
        ClockManager::new(clock, time_source_manager, rtc, notifier, diagnostics, track)
            .maintain_clock()
            .await
    }

    // TODO(jsankey): Once the network availability detection code is in the time sources (so we
    // no longer need to wait for network in between initializing the clock from rtc and
    // communicating with a time source) add an `execute_from_rtc` method that populates the clock
    // with the rtc value before beginning the maintain clock method.

    /// Construct a new `ClockManager`.
    fn new(
        clock: zx::Clock,
        time_source_manager: TimeSourceManager<T, D, KernelMonotonicProvider>,
        rtc: Option<R>,
        notifier: Option<Notifier>,
        diagnostics: Arc<D>,
        track: Track,
    ) -> Self {
        ClockManager {
            clock,
            time_source_manager,
            estimator: None,
            rtc,
            notifier,
            diagnostics,
            track,
        }
    }

    /// Maintain the clock indefinitely. This future will never complete.
    async fn maintain_clock(mut self) {
        let details = self.clock.get_details().expect("failed to get UTC clock details");
        let mut clock_offset = zx::Time::from_nanos(details.mono_to_synthetic.synthetic_offset)
            - zx::Time::from_nanos(details.mono_to_synthetic.reference_offset);
        let mut clock_started =
            details.backstop.into_nanos() != details.ticks_to_synthetic.synthetic_offset;
        std::mem::drop(details);

        loop {
            // Acquire a new sample.
            let sample = self.time_source_manager.next_sample().await;

            // Feed it to the estimator (or initialize the estimator).
            match &mut self.estimator {
                Some(estimator) => estimator.update(sample),
                None => self.estimator = Some(Estimator::new(self.track, sample)),
            }
            // Note: Both branches of the match led to a populated estimator so safe to unwrap.
            let estimator: &mut Estimator = &mut self.estimator.as_mut().unwrap();

            // Determine the error in the current clock compared to this updated estimate.
            // Note: In the initial implementation of `Estimator` updates are discarded hence
            //       error will be zero for the second and all subsequent samples.
            let reference_mono = zx::Time::get(zx::ClockId::Monotonic);
            let estimate_utc = estimator.estimate(reference_mono);
            let estimate_offset = estimate_utc - reference_mono;
            let clock_error = zx::Duration::from_nanos(
                (clock_offset.into_nanos() - estimate_offset.into_nanos()).abs(),
            );
            if clock_error < CLOCK_UPDATE_THRESHOLD {
                info!(
                    "updated {:?} offset of {:?} close to previous offset of {:?}, skipping update",
                    self.track,
                    estimate_offset.into_nanos(),
                    clock_offset.into_nanos()
                );
                continue;
            }

            // For now we implement all time corrections as a simple step to the intended time.
            let utc_chrono = Utc.timestamp_nanos(estimate_utc.into_nanos());
            if let Err(status) = self.clock.update(zx::ClockUpdate::new().value(estimate_utc)) {
                error!("failed to update {:?} clock to {}: {}", self.track, utc_chrono, status);
                continue;
            }
            clock_offset = estimate_offset;
            if !clock_started {
                self.diagnostics.record(Event::StartClock {
                    track: self.track,
                    source: StartClockSource::External(self.time_source_manager.role()),
                });
                info!("started {:?} clock from external source at {}", self.track, utc_chrono);
                clock_started = true;
            } else {
                self.diagnostics.record(Event::UpdateClock { track: self.track });
                info!("adjusted {:?} clock to {}", self.track, utc_chrono);
            }

            // Update the RTC clock if we have one.
            // Note this only applies to primary so we don't include the track in our log messages.
            if let Some(ref rtc) = self.rtc {
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

            // And trigger the notifier if we have one.
            if let Some(ref mut notifier) = self.notifier {
                notifier.set_source(ftime::UtcSource::External).await;
            }

            if self.track == Track::Primary {
                self.log_utc_offset();
            }
        }
    }

    /// Log a line in a specific format that it used by automated tooling to convert log times.
    fn log_utc_offset(&self) {
        // TODO(jsankey): Remove this function once the tooling has ceased to use it,
        //                estimate end October 2020 (b/169868836).
        let monotonic_before = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
        let utc_now = self.clock.read().map_or(0, |time| time.into_nanos());
        let monotonic_after = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
        info!(
            "CF-884:monotonic_before={}:utc={}:monotonic_after={}",
            monotonic_before, utc_now, monotonic_after,
        );
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            diagnostics::FakeDiagnostics,
            enums::{Role, WriteRtcOutcome},
            rtc::FakeRtc,
            time_source::{Event as TimeSourceEvent, FakeTimeSource, Sample},
        },
        fidl_fuchsia_time_external::{self as ftexternal, Status},
        fuchsia_async as fasync,
        fuchsia_zircon::{self as zx, HandleBased as _},
        futures::FutureExt,
        lazy_static::lazy_static,
        std::task::Poll,
        test_util::{assert_geq, assert_leq},
    };

    const NANOS_PER_SECOND: i64 = 1_000_000_000;

    const TEST_ROLE: Role = Role::Primary;

    const SAMPLE_SPACING: zx::Duration = zx::Duration::from_millis(100);
    const OFFSET: zx::Duration = zx::Duration::from_seconds(1111_000);
    const OFFSET_2: zx::Duration = zx::Duration::from_seconds(2222_000);

    lazy_static! {
        static ref TEST_TRACK: Track = Track::from(TEST_ROLE);
        static ref BACKSTOP_TIME: zx::Time = zx::Time::from_nanos(222222 * NANOS_PER_SECOND);
        static ref CLOCK_OPTS: zx::ClockOpts = zx::ClockOpts::empty();
        static ref START_CLOCK_SOURCE: StartClockSource = StartClockSource::External(TEST_ROLE);
    }

    /// Creates and starts a new clock with default options.
    fn create_clock() -> zx::Clock {
        let clock = zx::Clock::create(*CLOCK_OPTS, Some(*BACKSTOP_TIME)).unwrap();
        clock.update(zx::ClockUpdate::new().value(*BACKSTOP_TIME)).unwrap();
        clock
    }

    /// Creates a new `ClockManager` from a time source manager that outputs the supplied samples.
    fn create_clock_manager(
        clock: &zx::Clock,
        samples: Vec<Sample>,
        rtc: Option<FakeRtc>,
        notifier: Option<Notifier>,
        diagnostics: Arc<FakeDiagnostics>,
    ) -> ClockManager<FakeTimeSource, FakeRtc, FakeDiagnostics> {
        let mut events: Vec<TimeSourceEvent> =
            samples.into_iter().map(|sample| TimeSourceEvent::from(sample)).collect();
        events.insert(0, TimeSourceEvent::StatusChange { status: ftexternal::Status::Ok });
        let time_source = FakeTimeSource::events(events);
        let time_source_manager = TimeSourceManager::new_with_delays_disabled(
            *BACKSTOP_TIME,
            TEST_ROLE,
            time_source,
            Arc::clone(&diagnostics),
        );
        ClockManager::new(
            clock.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap(),
            time_source_manager,
            rtc,
            notifier,
            diagnostics,
            *TEST_TRACK,
        )
    }

    #[test]
    fn single_update_with_rtc_and_notifier() {
        let mut executor = fasync::Executor::new().unwrap();

        let clock = create_clock();
        let rtc = FakeRtc::valid(*BACKSTOP_TIME);
        let diagnostics = Arc::new(FakeDiagnostics::new());

        // Spawn test notifier and verify the initial state
        let (utc, utc_requests) =
            fidl::endpoints::create_proxy_and_stream::<ftime::UtcMarker>().unwrap();
        let notifier = Notifier::new(ftime::UtcSource::Backstop);
        notifier.handle_request_stream(utc_requests);
        let mut fut1 = async { utc.watch_state().await.unwrap().source.unwrap() }.boxed();
        assert_eq!(executor.run_until_stalled(&mut fut1), Poll::Ready(ftime::UtcSource::Backstop));

        // Create a clock manager
        let monotonic_ref = zx::Time::get(zx::ClockId::Monotonic);
        let clock_manager = create_clock_manager(
            &clock,
            vec![Sample::new(monotonic_ref + OFFSET, monotonic_ref)],
            Some(rtc.clone()),
            Some(notifier),
            Arc::clone(&diagnostics),
        );

        // Maintain the clock until no more work remains
        let monotonic_before = zx::Time::get(zx::ClockId::Monotonic);
        let mut fut2 = clock_manager.maintain_clock().boxed();
        let _ = executor.run_until_stalled(&mut fut2);
        let updated_utc = clock.read().unwrap();
        let monotonic_after = zx::Time::get(zx::ClockId::Monotonic);

        // Check that the clocks and reported time source have been updated. The UTC
        // should be bounded by the offset we supplied added to the monotonic window in which the
        // calculation took place.
        let mut fut3 = async { utc.watch_state().await.unwrap().source.unwrap() }.boxed();
        assert_eq!(executor.run_until_stalled(&mut fut3), Poll::Ready(ftime::UtcSource::External));
        assert_geq!(updated_utc, monotonic_before + OFFSET);
        assert_leq!(updated_utc, monotonic_after + OFFSET);
        assert_geq!(rtc.last_set().unwrap(), monotonic_before + OFFSET);
        assert_leq!(rtc.last_set().unwrap(), monotonic_after + OFFSET);

        // Check that the correct diagnostic events were logged.
        diagnostics.assert_events(&[
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
            Event::StartClock { track: *TEST_TRACK, source: *START_CLOCK_SOURCE },
            Event::WriteRtc { outcome: WriteRtcOutcome::Succeeded },
        ]);
    }

    #[test]
    fn single_update_without_rtc_and_notifier() {
        let mut executor = fasync::Executor::new().unwrap();

        let clock = create_clock();
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let monotonic_ref = zx::Time::get(zx::ClockId::Monotonic);
        let clock_manager = create_clock_manager(
            &clock,
            vec![Sample::new(monotonic_ref + OFFSET, monotonic_ref)],
            None,
            None,
            Arc::clone(&diagnostics),
        );

        // Maintain the clock until no more work remains
        let monotonic_before = zx::Time::get(zx::ClockId::Monotonic);
        let mut fut = clock_manager.maintain_clock().boxed();
        let _ = executor.run_until_stalled(&mut fut);
        let updated_utc = clock.read().unwrap();
        let monotonic_after = zx::Time::get(zx::ClockId::Monotonic);

        // Check that the clock has been updated. The UTC should be bounded by the offset we
        // supplied added to the monotonic window in which the calculation took place.
        assert_geq!(updated_utc, monotonic_before + OFFSET);
        assert_leq!(updated_utc, monotonic_after + OFFSET);

        // Check that the correct diagnostic events were logged.
        diagnostics.assert_events(&[
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
            Event::StartClock { track: *TEST_TRACK, source: *START_CLOCK_SOURCE },
        ]);
    }

    #[test]
    fn subsequent_updates_ignored() {
        let mut executor = fasync::Executor::new().unwrap();

        let clock = create_clock();
        let diagnostics = Arc::new(FakeDiagnostics::new());
        let monotonic_ref = zx::Time::get(zx::ClockId::Monotonic);
        let clock_manager = create_clock_manager(
            &clock,
            vec![
                Sample::new(
                    monotonic_ref - SAMPLE_SPACING + OFFSET,
                    monotonic_ref - SAMPLE_SPACING,
                ),
                Sample::new(monotonic_ref + OFFSET_2, monotonic_ref),
            ],
            None,
            None,
            Arc::clone(&diagnostics),
        );

        // Maintain the clock until no more work remains
        let monotonic_before = zx::Time::get(zx::ClockId::Monotonic);
        let mut fut = clock_manager.maintain_clock().boxed();
        let _ = executor.run_until_stalled(&mut fut);
        let updated_utc = clock.read().unwrap();
        let monotonic_after = zx::Time::get(zx::ClockId::Monotonic);

        // Check that the clock has been updated based on the first sample. The UTC should be
        // bounded by the offset we supplied added to the monotonic window in which the calculation
        // took place.
        assert_geq!(updated_utc, monotonic_before + OFFSET);
        assert_leq!(updated_utc, monotonic_after + OFFSET);

        // Check that the correct diagnostic events were logged.
        diagnostics.assert_events(&[
            Event::TimeSourceStatus { role: TEST_ROLE, status: Status::Ok },
            Event::StartClock { track: *TEST_TRACK, source: *START_CLOCK_SOURCE },
        ]);
    }
}
