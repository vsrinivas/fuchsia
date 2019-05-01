// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A networking stack.

#![feature(async_await, await_macro)]
#![deny(missing_docs)]
#![deny(unreachable_patterns)]

mod eventloop;
mod fidl_worker;

use crate::eventloop::EventLoop;

fn main() -> Result<(), failure::Error> {
    fuchsia_syslog::init()?;
    // Severity is set to debug during development.
    fuchsia_syslog::set_severity(-1);

    let mut executor = fuchsia_async::Executor::new()?;

    let eventloop = EventLoop::new();
    executor.run_singlethreaded(eventloop.run())
}
