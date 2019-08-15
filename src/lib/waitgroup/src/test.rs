// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use fuchsia_async as fasync;
use futures::{self, task::Poll, FutureExt};
use waitgroup::*;

#[fasync::run_singlethreaded]
#[test]
async fn does_not_terminate_early() {
    let mut wg = WaitGroup::new();

    {
        let _waiter = wg.new_waiter();
    }
    let _waiter = wg.new_waiter();

    let wait_fut = wg.wait().fuse();
    fasync::pin_mut!(wait_fut);
    assert_eq!(futures::poll!(wait_fut), Poll::Pending);
}

#[fasync::run_singlethreaded]
#[test]
async fn terminates_with_dropped_waiters() {
    let mut wg = WaitGroup::new();

    {
        let _waiter = wg.new_waiter();
        let _other_waiter = wg.new_waiter();
    }

    let wait_fut = wg.wait().fuse();
    fasync::pin_mut!(wait_fut);
    assert_eq!(futures::poll!(wait_fut), Poll::Ready(()));
}
