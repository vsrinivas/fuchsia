// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    create_cobalt_event_stream, new_clock, FakeClockController, NestedTimekeeper, PushSourcePuppet,
    STD_DEV, VALID_TIME,
};
use fidl_fuchsia_cobalt_test::{LogMethod, LoggerQuerierProxy};
use fidl_fuchsia_testing::Increment;
use fidl_fuchsia_time_external::TimeSample;
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx};
use futures::{Future, StreamExt};
use std::sync::Arc;
use test_util::assert_geq;
use time_metrics_registry::{
    TimekeeperTimeSourceEventsMetricDimensionEventType as TimeSourceEvent,
    TIMEKEEPER_TIME_SOURCE_EVENTS_METRIC_ID,
};

/// Run a test against an instance of timekeeper with fake time. Timekeeper will maintain the
/// provided clock.
///
/// Note that while timekeeper is run with fake time, calls to directly read time
/// on the test component retrieve the real time. When the test component needs to read fake
/// time, it must do so using the `FakeClockController` handle. Basically, tests should access
/// fake time through fake_clock_controller.get_monotonic() instead of the common methods such as
/// zx::Time::get_monotonic().
///
/// The provided `test_fn` is provided with handles to manipulate the time source, observe events
/// passed to cobalt, and manipulate the fake time.
fn faketime_test<F, Fut>(clock: Arc<zx::Clock>, test_fn: F)
where
    F: FnOnce(PushSourcePuppet, LoggerQuerierProxy, FakeClockController) -> Fut,
    Fut: Future<Output = ()>,
{
    // This result is dropped as init() fails if called multiple times in a process.
    // TODO(satsukiu): use fuchsia::test instead which handles this detail.
    let _ = fuchsia_syslog::init();
    let mut executor = fasync::Executor::new().expect("Failed to create executor");
    executor.run_singlethreaded(async move {
        let clock_arc = Arc::new(clock);
        let (timekeeper, push_source_controller, _, cobalt, fake_clock) =
            NestedTimekeeper::new(Arc::clone(&clock_arc), None, true /* use fake time */);
        test_fn(push_source_controller, cobalt, fake_clock.unwrap()).await;
        timekeeper.teardown().await;
    });
}

#[test]
fn test_kill_inactive_time_source() {
    let clock = new_clock();
    faketime_test(Arc::clone(&clock), |mut push_source_controller, cobalt, fake_time| async move {
        let cobalt_event_stream =
            create_cobalt_event_stream(Arc::new(cobalt), LogMethod::LogCobaltEvent);

        push_source_controller
            .set_sample(TimeSample {
                utc: Some(VALID_TIME.into_nanos()),
                monotonic: Some(fake_time.get_monotonic().await.expect("Failed to get time")),
                standard_deviation: Some(STD_DEV.into_nanos()),
                ..TimeSample::EMPTY
            })
            .await;
        fasync::OnSignals::new(&*clock, zx::Signals::CLOCK_STARTED)
            .await
            .expect("Failed to wait for CLOCK_STARTED");

        // Timekeeper should restart the time source after approximately an hour of inactivity.
        // Here, we run time at 60,000x rather than leaping forward in one step. This is done as
        // Timekeeper reads the time, then calculates an hour from there. If time is jumped
        // forward in a single step, the timeout will not occur in the case Timekeeper reads the
        // time after we jump time forward.
        fake_time.pause().await.expect("Failed to pause time");
        let mono_before =
            zx::Time::from_nanos(fake_time.get_monotonic().await.expect("Failed to get time"));
        fake_time
            .resume_with_increments(
                zx::Duration::from_millis(1).into_nanos(),
                &mut Increment::Determined(zx::Duration::from_minutes(1).into_nanos()),
            )
            .await
            .expect("Failed to resume time")
            .expect("Resume returned error");

        // Wait for Timekeeper to report the restarted event.
        let restart_event = cobalt_event_stream
            .skip_while(|event| {
                let is_restart_event = event.metric_id == TIMEKEEPER_TIME_SOURCE_EVENTS_METRIC_ID
                    && event
                        .event_codes
                        .contains(&(TimeSourceEvent::RestartedSampleTimeOut as u32));
                futures::future::ready(!is_restart_event)
            })
            .next()
            .await
            .expect("Failed to get restart event");
        assert_eq!(restart_event.metric_id, TIMEKEEPER_TIME_SOURCE_EVENTS_METRIC_ID);
        assert!(restart_event
            .event_codes
            .contains(&(TimeSourceEvent::RestartedSampleTimeOut as u32)));

        // At least an hour should've passed.
        let mono_after =
            zx::Time::from_nanos(fake_time.get_monotonic().await.expect("Failed to get time"));
        assert_geq!(mono_after, mono_before + zx::Duration::from_minutes(60));
    });
}
