// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A networking stack.
#![deny(missing_docs)]
#![deny(unreachable_patterns)]
#![recursion_limit = "256"]

// TODO(joshlf): Remove this once the old packet crate has been deleted and the
// new one's name has been changed back to `packet`.
extern crate packet_new as packet;

mod devices;
mod eventloop;
mod fidl_worker;

use crate::eventloop::EventLoop;

fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init()?;
    // Severity is set to debug during development.
    fuchsia_syslog::set_severity(-1);

    let mut executor = fuchsia_async::Executor::new()?;

    let eventloop = EventLoop::new();
    executor.run_singlethreaded(eventloop.run())
}
