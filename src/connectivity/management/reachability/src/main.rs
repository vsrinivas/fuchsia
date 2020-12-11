// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The reachability monitor monitors reachability state and generates an event to signal
//! changes.

extern crate fuchsia_syslog as syslog;
#[macro_use]
extern crate log;
use anyhow::Context as _;
use fuchsia_async as fasync;
use reachability_core::{ping_fut, IcmpPinger, Monitor};

mod eventloop;
mod worker;

use crate::eventloop::EventLoop;

fn main() -> Result<(), anyhow::Error> {
    // TODO(dpradilla): use a `StructOpt` to pass in a log level option where the user can control
    // how verbose logs should be.

    syslog::init_with_tags(&["reachability"]).expect("failed to initialize logger");

    info!("Starting reachability monitor!");
    let mut executor = fuchsia_async::Executor::new()?;

    let (request_tx, request_rx) = futures::channel::mpsc::unbounded();
    let (response_tx, response_rx) = futures::channel::mpsc::unbounded();
    let ping_task = fasync::Task::blocking(ping_fut(request_rx, response_tx));

    info!("collecting initial state.");
    let mut eventloop =
        EventLoop::new(Monitor::new(Box::new(IcmpPinger::new(request_tx, response_rx)))?);
    let () = executor
        .run_singlethreaded(eventloop.populate_state())
        .context("failed to populate initial reachability states")?;

    info!("monitoring");
    let eventloop_fut = eventloop.run();
    futures::pin_mut!(eventloop_fut);
    match executor.run_singlethreaded(futures::future::select(eventloop_fut, ping_task)) {
        futures::future::Either::Left(((), _)) => {
            panic!("event loop ended unexpectedly");
        }
        futures::future::Either::Right((ping_res, _)) => {
            panic!("ping backend ended unexpectedly with: {:?}", ping_res);
        }
    }
}
