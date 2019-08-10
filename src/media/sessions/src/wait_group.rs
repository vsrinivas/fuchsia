// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::{self, AbortHandle, Abortable, Pending};

#[derive(Debug)]
enum Never {}

/// A futures-aware wait group so one task can await the completion of many others.
#[derive(Debug)]
pub struct WaitGroup {
    members: Vec<Abortable<Pending<Never>>>,
}

/// A member of a `WaitGroup` that each task should hold, and drop when it is complete.
pub struct Waiter {
    handle: AbortHandle,
}

impl Drop for Waiter {
    fn drop(&mut self) {
        self.handle.abort();
    }
}

impl WaitGroup {
    pub fn new() -> Self {
        Self { members: vec![] }
    }

    pub fn new_waiter(&mut self) -> Waiter {
        let (fut, handle) = future::abortable(future::pending::<Never>());
        self.members.push(fut);
        Waiter { handle }
    }

    pub async fn wait(&mut self) {
        let _ = future::join_all(self.members.split_off(0)).await;
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async as fasync;
    use futures::{self, task::Poll, FutureExt};

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
}
