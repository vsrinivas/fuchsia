// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_fakeclock_test::ExampleMarker;
use fidl_fuchsia_testing::{FakeClockControlMarker, Increment};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;

const ONE_DAY: zx::Duration = zx::Duration::from_hours(24);

#[fasync::run_singlethreaded(test)]
async fn test_pause_advance() {
    let fake_time = connect_to_service::<FakeClockControlMarker>()
        .expect("failed to connect to FakeClockControl");
    let example =
        connect_to_service::<ExampleMarker>().expect("failed to connect to Example service");

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
}
