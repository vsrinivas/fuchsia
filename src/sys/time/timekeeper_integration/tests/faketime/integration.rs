// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_cobalt_test::{LogMethod, LoggerQuerierProxy};
use fidl_fuchsia_testing::Increment;
use fidl_fuchsia_time_external::{Status, TimeSample};
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx};
use futures::{Future, StreamExt};
use std::sync::Arc;
use test_util::assert_geq;
use time_metrics_registry::{
    TimekeeperTimeSourceEventsMetricDimensionEventType as TimeSourceEvent,
    TIMEKEEPER_TIME_SOURCE_EVENTS_METRIC_ID,
};
use timekeeper_integration_lib::{
    create_cobalt_event_stream, new_clock, poll_until, poll_until_async, FakeClockController,
    NestedTimekeeper, PushSourcePuppet, STD_DEV, VALID_TIME,
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
    F: FnOnce(Arc<PushSourcePuppet>, LoggerQuerierProxy, FakeClockController) -> Fut,
    Fut: Future<Output = ()>,
{
    let mut executor = fasync::LocalExecutor::new().expect("Failed to create executor");
    executor.run_singlethreaded(async move {
        let clock_arc = Arc::new(clock);
        let (timekeeper, push_source_controller, _, cobalt, fake_clock) =
            NestedTimekeeper::new(Arc::clone(&clock_arc), None, true /* use fake time */);
        test_fn(push_source_controller, cobalt, fake_clock.unwrap()).await;
        timekeeper.teardown().await;
    });
}

/// Start freely running the fake time at 60,000x the real time.
async fn freerun_time_fast(fake_clock: &FakeClockController) {
    fake_clock.pause().await.expect("Failed to pause time");
    fake_clock
        .resume_with_increments(
            zx::Duration::from_millis(1).into_nanos(),
            &mut Increment::Determined(zx::Duration::from_minutes(1).into_nanos()),
        )
        .await
        .expect("Failed to resume time")
        .expect("Resume returned error");
}

/// The duration after which timekeeper restarts an inactive time source.
const INACTIVE_SOURCE_RESTART_DURATION: zx::Duration = zx::Duration::from_hours(1);

#[fuchsia::test]
fn test_restart_inactive_time_source_that_claims_healthy() {
    let clock = new_clock();
    faketime_test(Arc::clone(&clock), |push_source_controller, cobalt, fake_time| async move {
        let cobalt_event_stream =
            create_cobalt_event_stream(Arc::new(cobalt), LogMethod::LogCobaltEvent);

        let mono_before =
            zx::Time::from_nanos(fake_time.get_monotonic().await.expect("Failed to get time"));
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

        assert_eq!(push_source_controller.lifetime_served_connections(), 1);

        // Timekeeper should restart the time source after approximately an hour of inactivity.
        // Here, we run time quickly rather than leaping forward in one step. This is done as
        // Timekeeper reads the time, then calculates an hour from there. If time is jumped
        // forward in a single step, the timeout will not occur in the case Timekeeper reads the
        // time after we jump time forward.
        freerun_time_fast(&fake_time).await;

        // Wait for Timekeeper to restart the time source. This is visible to the test as a second
        // connection to the fake time source.
        poll_until(|| push_source_controller.lifetime_served_connections() > 1).await;

        // At least an hour should've passed.
        let mono_after =
            zx::Time::from_nanos(fake_time.get_monotonic().await.expect("Failed to get time"));
        assert_geq!(mono_after, mono_before + INACTIVE_SOURCE_RESTART_DURATION);

        // Timekeeper should report the restarted event to Cobalt.
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
    });
}

#[fuchsia::test]
fn test_dont_restart_inactive_time_source_with_unhealthy_dependency() {
    let clock = new_clock();
    faketime_test(Arc::clone(&clock), |push_source_controller, _, fake_time| async move {
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
        // Report unhealthy after first sample accepted.
        push_source_controller.set_status(Status::Network).await;

        freerun_time_fast(&fake_time).await;

        // Wait longer than the usual restart duration.
        let mono_before =
            zx::Time::from_nanos(fake_time.get_monotonic().await.expect("Failed to get time"));
        poll_until_async(|| async {
            let mono_now =
                zx::Time::from_nanos(fake_time.get_monotonic().await.expect("Failed to get time"));
            mono_now - mono_before > INACTIVE_SOURCE_RESTART_DURATION * 4
        })
        .await;

        // Since there should be no restart attempts, only one connection was made to our fake.
        assert_eq!(push_source_controller.lifetime_served_connections(), 1);
    });
}
