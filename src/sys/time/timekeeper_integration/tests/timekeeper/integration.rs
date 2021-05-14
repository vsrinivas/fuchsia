// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_cobalt::CobaltEvent;
use fidl_fuchsia_cobalt_test::{LogMethod, LoggerQuerierProxy};
use fidl_fuchsia_time_external::TimeSample;
use fuchsia_async as fasync;
use fuchsia_cobalt::CobaltEventExt;
use fuchsia_zircon::{self as zx};
use futures::{stream::StreamExt, Future};
use std::sync::Arc;
use test_util::{assert_geq, assert_leq, assert_lt};
use time_metrics_registry::{
    RealTimeClockEventsMetricDimensionEventType as RtcEventType,
    TimeMetricDimensionExperiment as Experiment, TimeMetricDimensionTrack as Track,
    TimekeeperLifecycleEventsMetricDimensionEventType as LifecycleEventType,
    TimekeeperTimeSourceEventsMetricDimensionEventType as TimeSourceEvent,
    TimekeeperTrackEventsMetricDimensionEventType as TrackEvent, REAL_TIME_CLOCK_EVENTS_METRIC_ID,
    TIMEKEEPER_CLOCK_CORRECTION_METRIC_ID, TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID,
    TIMEKEEPER_SQRT_COVARIANCE_METRIC_ID, TIMEKEEPER_TIME_SOURCE_EVENTS_METRIC_ID,
    TIMEKEEPER_TRACK_EVENTS_METRIC_ID,
};
use timekeeper_integration_lib::{
    create_cobalt_event_stream, new_clock, poll_until, poll_until_some, rtc_time_to_zx_time,
    NestedTimekeeper, PushSourcePuppet, RtcUpdates, BACKSTOP_TIME, BEFORE_BACKSTOP_TIME,
    BETWEEN_SAMPLES, STD_DEV, VALID_RTC_TIME, VALID_TIME, VALID_TIME_2,
};

/// Run a test against an instance of timekeeper. Timekeeper will maintain the provided clock.
/// If `initial_rtc_time` is provided, a fake RTC device that reports the time as
/// `initial_rtc_time` is injected into timekeeper's environment. The provided `test_fn` is
/// provided with handles to manipulate the time source and observe changes to the RTC and cobalt.
fn timekeeper_test<F, Fut>(clock: Arc<zx::Clock>, initial_rtc_time: Option<zx::Time>, test_fn: F)
where
    F: FnOnce(Arc<PushSourcePuppet>, RtcUpdates, LoggerQuerierProxy) -> Fut,
    Fut: Future,
{
    let mut executor = fasync::LocalExecutor::new().unwrap();
    executor.run_singlethreaded(async move {
        let clock_arc = Arc::new(clock);
        let (timekeeper, push_source_controller, rtc, cobalt, _) = NestedTimekeeper::new(
            Arc::clone(&clock_arc),
            initial_rtc_time,
            false, // no fake clock.
        );
        test_fn(push_source_controller, rtc, cobalt).await;
        timekeeper.teardown().await;
    });
}

#[fuchsia::test]
fn test_no_rtc_start_clock_from_time_source() {
    let clock = new_clock();
    timekeeper_test(Arc::clone(&clock), None, |push_source_controller, _, cobalt| async move {
        let before_update_ticks = clock.get_details().unwrap().last_value_update_ticks;

        let sample_monotonic = zx::Time::get_monotonic();
        push_source_controller
            .set_sample(TimeSample {
                utc: Some(VALID_TIME.into_nanos()),
                monotonic: Some(sample_monotonic.into_nanos()),
                standard_deviation: Some(STD_DEV.into_nanos()),
                ..TimeSample::EMPTY
            })
            .await;

        fasync::OnSignals::new(&*clock, zx::Signals::CLOCK_STARTED).await.unwrap();
        let after_update_ticks = clock.get_details().unwrap().last_value_update_ticks;
        assert!(after_update_ticks > before_update_ticks);

        // UTC time reported by the clock should be at least the time in the sample and no
        // more than the UTC time in the sample + time elapsed since the sample was created.
        let reported_utc = clock.read().unwrap();
        let monotonic_after_update = zx::Time::get_monotonic();
        assert_geq!(reported_utc, *VALID_TIME);
        assert_leq!(reported_utc, *VALID_TIME + (monotonic_after_update - sample_monotonic));

        let cobalt_event_stream =
            create_cobalt_event_stream(Arc::new(cobalt), LogMethod::LogCobaltEvent);
        assert_eq!(
            cobalt_event_stream.take(5).collect::<Vec<_>>().await,
            vec![
                CobaltEvent::builder(REAL_TIME_CLOCK_EVENTS_METRIC_ID)
                    .with_event_codes(RtcEventType::NoDevices)
                    .as_event(),
                CobaltEvent::builder(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID)
                    .with_event_codes(LifecycleEventType::InitializedBeforeUtcStart)
                    .as_event(),
                CobaltEvent::builder(TIMEKEEPER_TRACK_EVENTS_METRIC_ID)
                    .with_event_codes((
                        TrackEvent::EstimatedOffsetUpdated,
                        Track::Primary,
                        Experiment::None
                    ))
                    .as_count_event(0, 1),
                CobaltEvent::builder(TIMEKEEPER_SQRT_COVARIANCE_METRIC_ID)
                    .with_event_codes((Track::Primary, Experiment::None))
                    .as_count_event(0, STD_DEV.into_micros()),
                CobaltEvent::builder(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID)
                    .with_event_codes(LifecycleEventType::StartedUtcFromTimeSource)
                    .as_event(),
            ]
        );
    });
}

#[fuchsia::test]
fn test_invalid_rtc_start_clock_from_time_source() {
    let clock = new_clock();
    timekeeper_test(
        Arc::clone(&clock),
        Some(*BEFORE_BACKSTOP_TIME),
        |push_source_controller, rtc_updates, cobalt| async move {
            let mut cobalt_event_stream =
                create_cobalt_event_stream(Arc::new(cobalt), LogMethod::LogCobaltEvent);
            // Timekeeper should reject the RTC time.
            assert_eq!(
                cobalt_event_stream.by_ref().take(2).collect::<Vec<CobaltEvent>>().await,
                vec![
                    CobaltEvent::builder(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID)
                        .with_event_codes(LifecycleEventType::InitializedBeforeUtcStart)
                        .as_event(),
                    CobaltEvent::builder(REAL_TIME_CLOCK_EVENTS_METRIC_ID)
                        .with_event_codes(RtcEventType::ReadInvalidBeforeBackstop)
                        .as_event()
                ]
            );

            let sample_monotonic = zx::Time::get_monotonic();
            push_source_controller
                .set_sample(TimeSample {
                    utc: Some(VALID_TIME.into_nanos()),
                    monotonic: Some(sample_monotonic.into_nanos()),
                    standard_deviation: Some(STD_DEV.into_nanos()),
                    ..TimeSample::EMPTY
                })
                .await;

            // Timekeeper should accept the time from the time source.
            fasync::OnSignals::new(&*clock, zx::Signals::CLOCK_STARTED).await.unwrap();
            // UTC time reported by the clock should be at least the time reported by the time
            // source, and no more than the UTC time reported by the time source + time elapsed
            // since the time was read.
            let reported_utc = clock.read().unwrap();
            let monotonic_after = zx::Time::get_monotonic();
            assert_geq!(reported_utc, *VALID_TIME);
            assert_leq!(reported_utc, *VALID_TIME + (monotonic_after - sample_monotonic));
            // RTC should also be set.
            let rtc_update = poll_until_some(|| rtc_updates.to_vec().pop()).await;
            let monotonic_after_rtc_set = zx::Time::get_monotonic();
            let rtc_reported_utc = rtc_time_to_zx_time(rtc_update);
            assert_geq!(rtc_reported_utc, *VALID_TIME);
            assert_leq!(
                rtc_reported_utc,
                *VALID_TIME + (monotonic_after_rtc_set - sample_monotonic)
            );
            assert_eq!(
                cobalt_event_stream.take(4).collect::<Vec<_>>().await,
                vec![
                    CobaltEvent::builder(TIMEKEEPER_TRACK_EVENTS_METRIC_ID)
                        .with_event_codes((
                            TrackEvent::EstimatedOffsetUpdated,
                            Track::Primary,
                            Experiment::None
                        ))
                        .as_count_event(0, 1),
                    CobaltEvent::builder(TIMEKEEPER_SQRT_COVARIANCE_METRIC_ID)
                        .with_event_codes((Track::Primary, Experiment::None))
                        .as_count_event(0, STD_DEV.into_micros()),
                    CobaltEvent::builder(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID)
                        .with_event_codes(LifecycleEventType::StartedUtcFromTimeSource)
                        .as_event(),
                    CobaltEvent::builder(REAL_TIME_CLOCK_EVENTS_METRIC_ID)
                        .with_event_codes(RtcEventType::WriteSucceeded)
                        .as_event()
                ]
            );
        },
    );
}

#[fuchsia::test]
fn test_start_clock_from_rtc() {
    let clock = new_clock();
    let monotonic_before = zx::Time::get_monotonic();
    timekeeper_test(
        Arc::clone(&clock),
        Some(*VALID_RTC_TIME),
        |push_source_controller, rtc_updates, cobalt| async move {
            let mut cobalt_event_stream =
                create_cobalt_event_stream(Arc::new(cobalt), LogMethod::LogCobaltEvent);

            // Clock should start from the time read off the RTC.
            fasync::OnSignals::new(&*clock, zx::Signals::CLOCK_STARTED).await.unwrap();

            // UTC time reported by the clock should be at least the time reported by the RTC, and no
            // more than the UTC time reported by the RTC + time elapsed since Timekeeper was launched.
            let reported_utc = clock.read().unwrap();
            let monotonic_after = zx::Time::get_monotonic();
            assert_geq!(reported_utc, *VALID_RTC_TIME);
            assert_leq!(reported_utc, *VALID_RTC_TIME + (monotonic_after - monotonic_before));

            assert_eq!(
                cobalt_event_stream.by_ref().take(3).collect::<Vec<CobaltEvent>>().await,
                vec![
                    CobaltEvent::builder(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID)
                        .with_event_codes(LifecycleEventType::InitializedBeforeUtcStart)
                        .as_event(),
                    CobaltEvent::builder(REAL_TIME_CLOCK_EVENTS_METRIC_ID)
                        .with_event_codes(RtcEventType::ReadSucceeded)
                        .as_event(),
                    CobaltEvent::builder(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID)
                        .with_event_codes(LifecycleEventType::StartedUtcFromRtc)
                        .as_event(),
                ]
            );

            // Clock should be updated again when the push source reports another time.
            let clock_last_set_ticks = clock.get_details().unwrap().last_value_update_ticks;
            let sample_monotonic = zx::Time::get_monotonic();
            push_source_controller
                .set_sample(TimeSample {
                    utc: Some(VALID_TIME.into_nanos()),
                    monotonic: Some(sample_monotonic.into_nanos()),
                    standard_deviation: Some(STD_DEV.into_nanos()),
                    ..TimeSample::EMPTY
                })
                .await;
            poll_until(|| {
                clock.get_details().unwrap().last_value_update_ticks != clock_last_set_ticks
            })
            .await;
            let clock_utc = clock.read().unwrap();
            let monotonic_after_read = zx::Time::get_monotonic();
            assert_geq!(clock_utc, *VALID_TIME);
            assert_leq!(clock_utc, *VALID_TIME + (monotonic_after_read - sample_monotonic));
            // RTC should be set too.
            let rtc_update = poll_until_some(|| rtc_updates.to_vec().pop()).await;
            let monotonic_after_rtc_set = zx::Time::get_monotonic();
            let rtc_reported_utc = rtc_time_to_zx_time(rtc_update);
            assert_geq!(rtc_reported_utc, *VALID_TIME);
            assert_leq!(
                rtc_reported_utc,
                *VALID_TIME + (monotonic_after_rtc_set - sample_monotonic)
            );

            assert_eq!(
                cobalt_event_stream.by_ref().take(3).collect::<Vec<CobaltEvent>>().await,
                vec![
                    CobaltEvent::builder(TIMEKEEPER_TRACK_EVENTS_METRIC_ID)
                        .with_event_codes((
                            TrackEvent::EstimatedOffsetUpdated,
                            Track::Primary,
                            Experiment::None
                        ))
                        .as_count_event(0, 1),
                    CobaltEvent::builder(TIMEKEEPER_SQRT_COVARIANCE_METRIC_ID)
                        .with_event_codes((Track::Primary, Experiment::None))
                        .as_count_event(0, STD_DEV.into_micros()),
                    CobaltEvent::builder(TIMEKEEPER_TRACK_EVENTS_METRIC_ID)
                        .with_event_codes((
                            TrackEvent::CorrectionByStep,
                            Track::Primary,
                            Experiment::None
                        ))
                        .as_count_event(0, 1),
                ]
            );

            // A correction value always follows a CorrectionBy* event. Verify metric type but rely
            // on unit test to verify content since we can't predict exactly what time will be used.
            assert_eq!(
                cobalt_event_stream.by_ref().take(1).collect::<Vec<CobaltEvent>>().await[0]
                    .metric_id,
                TIMEKEEPER_CLOCK_CORRECTION_METRIC_ID
            );

            assert_eq!(
                cobalt_event_stream.by_ref().take(2).collect::<Vec<CobaltEvent>>().await,
                vec![
                    CobaltEvent::builder(TIMEKEEPER_TRACK_EVENTS_METRIC_ID)
                        .with_event_codes((
                            TrackEvent::ClockUpdateTimeStep,
                            Track::Primary,
                            Experiment::None
                        ))
                        .as_count_event(0, 1),
                    CobaltEvent::builder(REAL_TIME_CLOCK_EVENTS_METRIC_ID)
                        .with_event_codes(RtcEventType::WriteSucceeded)
                        .as_event(),
                ]
            );
        },
    );
}

#[fuchsia::test]
fn test_reject_before_backstop() {
    let clock = new_clock();
    timekeeper_test(Arc::clone(&clock), None, |push_source_controller, _, cobalt| async move {
        let cobalt_event_stream =
            create_cobalt_event_stream(Arc::new(cobalt), LogMethod::LogCobaltEvent);

        push_source_controller
            .set_sample(TimeSample {
                utc: Some(BEFORE_BACKSTOP_TIME.into_nanos()),
                monotonic: Some(zx::Time::get_monotonic().into_nanos()),
                standard_deviation: Some(STD_DEV.into_nanos()),
                ..TimeSample::EMPTY
            })
            .await;

        // Wait for the sample rejected event to be sent to Cobalt.
        cobalt_event_stream
            .take_while(|event| {
                let is_reject_sample_event = event.metric_id
                    == TIMEKEEPER_TIME_SOURCE_EVENTS_METRIC_ID
                    && event
                        .event_codes
                        .contains(&(TimeSourceEvent::SampleRejectedBeforeBackstop as u32));
                futures::future::ready(is_reject_sample_event)
            })
            .collect::<Vec<_>>()
            .await;
        // Clock should still read backstop.
        assert_eq!(*BACKSTOP_TIME, clock.read().unwrap());
    });
}

#[fuchsia::test]
fn test_slew_clock() {
    // Constants for controlling the duration of the slew we want to induce. These constants
    // are intended to tune the test to avoid flakes and do not necessarily need to match up with
    // those in timekeeper.
    const SLEW_DURATION: zx::Duration = zx::Duration::from_minutes(90);
    const NOMINAL_SLEW_PPM: i64 = 20;
    let error_for_slew = SLEW_DURATION * NOMINAL_SLEW_PPM / 1_000_000;

    let clock = new_clock();
    timekeeper_test(Arc::clone(&clock), None, |push_source_controller, _, _| async move {
        // Let the first sample be slightly in the past so later samples are not in the future.
        let sample_1_monotonic = zx::Time::get_monotonic() - BETWEEN_SAMPLES;
        let sample_1_utc = *VALID_TIME;
        push_source_controller
            .set_sample(TimeSample {
                utc: Some(sample_1_utc.into_nanos()),
                monotonic: Some(sample_1_monotonic.into_nanos()),
                standard_deviation: Some(STD_DEV.into_nanos()),
                ..TimeSample::EMPTY
            })
            .await;

        // After the first sample, the clock is started, and running at the same rate as
        // the reference.
        fasync::OnSignals::new(&*clock, zx::Signals::CLOCK_STARTED).await.unwrap();
        let clock_rate = clock.get_details().unwrap().mono_to_synthetic.rate;
        assert_eq!(clock_rate.reference_ticks, clock_rate.synthetic_ticks);
        let last_generation_counter = clock.get_details().unwrap().generation_counter;

        // Push a second sample that indicates UTC running slightly behind monotonic.
        let sample_2_monotonic = sample_1_monotonic + BETWEEN_SAMPLES;
        let sample_2_utc = sample_1_utc + BETWEEN_SAMPLES - error_for_slew * 2;
        push_source_controller
            .set_sample(TimeSample {
                utc: Some(sample_2_utc.into_nanos()),
                monotonic: Some(sample_2_monotonic.into_nanos()),
                standard_deviation: Some(STD_DEV.into_nanos()),
                ..TimeSample::EMPTY
            })
            .await;

        // After the second sample, the clock is running slightly slower than the reference.
        poll_until(|| clock.get_details().unwrap().generation_counter != last_generation_counter)
            .await;
        let slew_rate = clock.get_details().unwrap().mono_to_synthetic.rate;
        assert_lt!(slew_rate.synthetic_ticks, slew_rate.reference_ticks);

        // TODO(fxbug.dev/65239) - verify that the slew completes.
    });
}

#[fuchsia::test]
fn test_step_clock() {
    const STEP_ERROR: zx::Duration = zx::Duration::from_hours(1);
    let clock = new_clock();
    timekeeper_test(Arc::clone(&clock), None, |push_source_controller, _, _| async move {
        // Let the first sample be slightly in the past so later samples are not in the future.
        let monotonic_before = zx::Time::get_monotonic();
        let sample_1_monotonic = monotonic_before - BETWEEN_SAMPLES;
        let sample_1_utc = *VALID_TIME;
        push_source_controller
            .set_sample(TimeSample {
                utc: Some(sample_1_utc.into_nanos()),
                monotonic: Some(sample_1_monotonic.into_nanos()),
                standard_deviation: Some(STD_DEV.into_nanos()),
                ..TimeSample::EMPTY
            })
            .await;

        // After the first sample, the clock is started, and running at the same rate as
        // the reference.
        fasync::OnSignals::new(&*clock, zx::Signals::CLOCK_STARTED).await.unwrap();
        let utc_now = clock.read().unwrap();
        let monotonic_after = zx::Time::get_monotonic();
        assert_geq!(utc_now, sample_1_utc + BETWEEN_SAMPLES);
        assert_leq!(utc_now, sample_1_utc + BETWEEN_SAMPLES + (monotonic_after - monotonic_before));

        let clock_last_set_ticks = clock.get_details().unwrap().last_value_update_ticks;

        let sample_2_monotonic = sample_1_monotonic + BETWEEN_SAMPLES;
        let sample_2_utc = sample_1_utc + BETWEEN_SAMPLES + STEP_ERROR;
        push_source_controller
            .set_sample(TimeSample {
                utc: Some(sample_2_utc.into_nanos()),
                monotonic: Some(sample_2_monotonic.into_nanos()),
                standard_deviation: Some(STD_DEV.into_nanos()),
                ..TimeSample::EMPTY
            })
            .await;
        poll_until(|| clock.get_details().unwrap().last_value_update_ticks != clock_last_set_ticks)
            .await;
        let utc_now_2 = clock.read().unwrap();
        let monotonic_after_2 = zx::Time::get_monotonic();

        // After the second sample, the clock should have jumped to an offset approximately halfway
        // between the offsets defined in the two samples. 500 ms is added to the upper bound as
        // the estimate takes more of the second sample into account (as the oscillator drift is
        // added to the uncertainty of the first sample).
        let jump_utc = sample_2_utc - STEP_ERROR / 2;
        assert_geq!(utc_now_2, jump_utc);
        assert_leq!(
            utc_now_2,
            jump_utc + (monotonic_after_2 - monotonic_before) + zx::Duration::from_millis(500)
        );
    });
}

fn avg(time_1: zx::Time, time_2: zx::Time) -> zx::Time {
    let time_1 = time_1.into_nanos() as i128;
    let time_2 = time_2.into_nanos() as i128;
    let avg = (time_1 + time_2) / 2;
    zx::Time::from_nanos(avg as i64)
}

#[fuchsia::test]
fn test_restart_crashed_time_source() {
    let clock = new_clock();
    timekeeper_test(Arc::clone(&clock), None, |push_source_controller, _, _| async move {
        // Let the first sample be slightly in the past so later samples are not in the future.
        let monotonic_before = zx::Time::get_monotonic();
        let sample_1_monotonic = monotonic_before - BETWEEN_SAMPLES;
        let sample_1_utc = *VALID_TIME;
        push_source_controller
            .set_sample(TimeSample {
                utc: Some(sample_1_utc.into_nanos()),
                monotonic: Some(sample_1_monotonic.into_nanos()),
                standard_deviation: Some(STD_DEV.into_nanos()),
                ..TimeSample::EMPTY
            })
            .await;

        // After the first sample, the clock is started.
        fasync::OnSignals::new(&*clock, zx::Signals::CLOCK_STARTED).await.unwrap();
        let last_generation_counter = clock.get_details().unwrap().generation_counter;

        // After a time source crashes, timekeeper should restart it and accept samples from it.
        push_source_controller.simulate_crash();
        let sample_2_utc = *VALID_TIME_2;
        let sample_2_monotonic = sample_1_monotonic + BETWEEN_SAMPLES;
        push_source_controller
            .set_sample(TimeSample {
                utc: Some(sample_2_utc.into_nanos()),
                monotonic: Some(sample_2_monotonic.into_nanos()),
                standard_deviation: Some(STD_DEV.into_nanos()),
                ..TimeSample::EMPTY
            })
            .await;
        poll_until(|| clock.get_details().unwrap().generation_counter != last_generation_counter)
            .await;
        // Time from clock should incorporate the second sample.
        let result_utc = clock.read().unwrap();
        let monotonic_after = zx::Time::get_monotonic();
        let minimum_expected = avg(sample_1_utc + BETWEEN_SAMPLES, sample_2_utc)
            + (monotonic_after - monotonic_before);
        assert_geq!(result_utc, minimum_expected);
    });
}
