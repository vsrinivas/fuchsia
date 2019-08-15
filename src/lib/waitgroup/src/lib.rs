// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![deny(missing_docs)]

//! A library with a futures-aware waitgroup.

use futures::future::{self, AbortHandle, Abortable, Pending};

#[derive(Debug)]
enum Never {}

/// A futures-aware wait group enabling one task to await the completion of many others.
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
    /// Creates a new waitgroup with no tasks to wait on.
    pub fn new() -> Self {
        Self { members: vec![] }
    }

    /// Returns a `Waiter` handle to give to tasks that must be waited on.
    pub fn new_waiter(&mut self) -> Waiter {
        let (fut, handle) = future::abortable(future::pending::<Never>());
        self.members.push(fut);
        Waiter { handle }
    }

    /// A wait that completes when all `Waiter`s vended by `new_waiter()` are dropped.
    pub async fn wait(&mut self) {
        let _ = future::join_all(self.members.split_off(0)).await;
    }
}
