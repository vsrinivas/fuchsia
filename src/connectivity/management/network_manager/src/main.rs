// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The router manager allows clients to manage router device properties.

#![feature(async_await, await_macro)]
#![deny(missing_docs)]
#![deny(unreachable_patterns)]

extern crate fuchsia_syslog as syslog;
#[macro_use]
extern crate log;

mod eventloop;
mod fidl_worker;
mod overnet_worker;
mod packet_filter;

use crate::eventloop::EventLoop;

fn main() -> Result<(), failure::Error> {
    syslog::init().expect("failed to initialize logger");
    // Severity is set to debug during development.
    fuchsia_syslog::set_severity(-2);

    info!("Starting Router Manager!");
    let mut executor = fuchsia_async::Executor::new()?;

    let eventloop = EventLoop::new()?;
    executor.run_singlethreaded(eventloop.run())
}
