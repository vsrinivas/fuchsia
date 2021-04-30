// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_fakeclock_test::ExampleMarker;
use fidl_fuchsia_testing::{DeadlineEventType, FakeClockControlMarker, Increment};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon as zx;
use named_timer::DeadlineId;

const DEADLINE_NAME: DeadlineId<'static> = DeadlineId::new("fake-clock-example", "deadline");

const ONE_DAY: zx::Duration = zx::Duration::from_hours(24);
const ONE_MILLIS: zx::Duration = zx::Duration::from_millis(1);
const ONE_HOUR: zx::Duration = zx::Duration::from_hours(1);

#[fasync::run_singlethreaded(test)]
async fn test_pause_advance() {
    let fake_time = connect_to_protocol::<FakeClockControlMarker>()
        .expect("failed to connect to FakeClockControl");
    let example =
        connect_to_protocol::<ExampleMarker>().expect("failed to connect to Example service");

    // When time is paused, querying for time twice should give the same result.
    let () = fake_time.pause().await.expect("failed to pause time");
    let now_1 = example.get_monotonic().await.expect("get_monotonic failed");
    let now_2 = example.get_monotonic().await.expect("get_monotonic failed");
    assert_eq!(now_1, now_2);

    // Set a 24 hour timer then advance time 24 hours. The timer should complete.
    let long_timeout = zx::Time::from_nanos(now_1) + ONE_DAY;
    let long_wait_fut = example.wait_until(long_timeout.into_nanos());
    let () = fake_time
        .advance(&mut Increment::Determined(ONE_DAY.into_nanos()))
        .await
        .expect("advance failed")
        .expect("advance returned error");
    let () = long_wait_fut.await.expect("wait_until failed");
    assert_eq!(
        example.get_monotonic().await.expect("get_monotonic failed"),
        long_timeout.into_nanos()
    );

    // Test named deadlines.
    // First, we set a stop point to detect when a timer is set, then resume time.
    let (deadline_set_event, deadline_set_server) =
        zx::EventPair::create().expect("failed to create eventpair");
    let () = fake_time
        .add_stop_point(&mut DEADLINE_NAME.into(), DeadlineEventType::Set, deadline_set_server)
        .await
        .expect("add_stop_point failed")
        .expect("add_stop_point returned error");
    let () = fake_time
        .resume_with_increments(
            ONE_MILLIS.into_nanos(),
            &mut Increment::Determined(ONE_MILLIS.into_nanos()),
        )
        .await
        .expect("resume_with_increments failed")
        .expect("resume_with_increments returned error");

    // Make a request that causes the timer to be set, and wait for the timer to be set.
    let long_wait_fut = example.wait_for(ONE_DAY.into_nanos());
    assert_eq!(
        fasync::OnSignals::new(&deadline_set_event, zx::Signals::EVENTPAIR_SIGNALED)
            .await
            .expect("waiting for timer set failed")
            & !zx::Signals::EVENTPAIR_CLOSED,
        zx::Signals::EVENTPAIR_SIGNALED
    );

    // Time should now be stopped.
    let now_1 = example.get_monotonic().await.expect("get_monotonic failed");
    let now_2 = example.get_monotonic().await.expect("get_monotonic failed");
    assert_eq!(now_1, now_2);

    // We now register a stop point to trigger when the timer expires, and run time fast.
    let (deadline_expire_event, deadline_expire_server) =
        zx::EventPair::create().expect("failed to create eventpair");
    let () = fake_time
        .add_stop_point(
            &mut DEADLINE_NAME.into(),
            DeadlineEventType::Expired,
            deadline_expire_server,
        )
        .await
        .expect("add_stop_point failed")
        .expect("add_stop_point returned error");
    let () = fake_time
        .resume_with_increments(
            ONE_MILLIS.into_nanos(),
            &mut Increment::Determined(ONE_HOUR.into_nanos()),
        )
        .await
        .expect("resume_with_increments failed")
        .expect("resume_with_increments returned error");

    // Wait for the timer to expire. The timer in the component under test should complete.
    assert_eq!(
        fasync::OnSignals::new(&deadline_expire_event, zx::Signals::EVENTPAIR_SIGNALED)
            .await
            .expect("waiting for timer expired failed")
            & !zx::Signals::EVENTPAIR_CLOSED,
        zx::Signals::EVENTPAIR_SIGNALED
    );
    let () = long_wait_fut.await.expect("wait_for failed");
    // A day should've passed.
    let now_3 = example.get_monotonic().await.expect("get_monotonic failed");
    assert!(now_3 >= now_2 + ONE_DAY.into_nanos());
}
