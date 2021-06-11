// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The reachability monitor monitors reachability state and generates an event to signal
//! changes.

// From https://docs.rs/futures/0.3.1/futures/macro.select.html:
//   Note that select! relies on proc-macro-hack, and may require to set the compiler's
//   recursion limit very high, e.g. #![recursion_limit="1024"].
#![recursion_limit = "512"]

extern crate fuchsia_syslog as syslog;
#[macro_use]
extern crate log;
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect::component;
use futures::{FutureExt as _, StreamExt as _};
use reachability_core::Monitor;

mod eventloop;

use crate::eventloop::EventLoop;

fn main() -> Result<(), anyhow::Error> {
    // TODO(dpradilla): use a `StructOpt` to pass in a log level option where the user can control
    // how verbose logs should be.

    syslog::init_with_tags(&["reachability"]).expect("failed to initialize logger");

    info!("Starting reachability monitor!");
    let mut executor = fuchsia_async::LocalExecutor::new()?;

    let mut fs = ServiceFs::new_local();
    let mut fs = fs.take_and_serve_directory_handle()?;

    let inspector = component::inspector();
    let () = inspect_runtime::serve(&inspector, &mut fs)?;

    let mut monitor = Monitor::new()?;
    let () = monitor.set_inspector(inspector);

    info!("monitoring");
    let mut eventloop = EventLoop::new(monitor);
    let eventloop_fut = eventloop.run().fuse();
    futures::pin_mut!(eventloop_fut);
    let mut serve_fut = fs.collect().map(Ok);
    executor.run_singlethreaded(async {
        loop {
            futures::select! {
                r = eventloop_fut => break r,
                r = serve_fut => break r,
            }
        }
    })
}
