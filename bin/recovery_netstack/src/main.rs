// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A networking stack.

#![feature(async_await, await_macro, futures_api)]
// In case we roll the toolchain and something we're using as a feature has been
// stabilized.
#![allow(stable_features)]
#![deny(missing_docs)]
#![deny(unreachable_patterns)]

mod eventloop;

use crate::eventloop::EventLoop;
use std::env;

fn main() -> Result<(), failure::Error> {
    fuchsia_syslog::init()?;
    // Severity is set to debug during development.
    fuchsia_syslog::set_severity(-1);

    let path = env::args()
        .nth(1)
        .unwrap_or_else(|| String::from(eventloop::DEFAULT_ETH));
    let eth_future = EventLoop::run_ethernet(&path);

    let mut executor = fuchsia_async::Executor::new()?;
    executor.run_singlethreaded(eth_future)
}
