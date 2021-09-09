// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async::TestExecutor;
use futures::pin_mut;
use futures::task::Poll;

/// Run the provided `future` via the `executor`.
pub fn move_executor_forward(
    executor: &mut TestExecutor,
    future: impl futures::Future<Output = ()>,
    panic_msg: &str,
) {
    pin_mut!(future);
    match executor.run_until_stalled(&mut future) {
        Poll::Ready(res) => res,
        _ => panic!("{}", panic_msg),
    }
}

/// Run the provided `future` via the `executor` and return the result of the future.
pub fn move_executor_forward_and_get<T>(
    executor: &mut TestExecutor,
    future: impl futures::Future<Output = T>,
    panic_msg: &str,
) -> T {
    pin_mut!(future);
    match executor.run_until_stalled(&mut future) {
        Poll::Ready(res) => res,
        _ => panic!("{}", panic_msg),
    }
}
