// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The reachability monitor monitors reachability state and generates an event to signal
//! changes.

use fuchsia_component::server::ServiceFs;
use fuchsia_inspect::component;
use futures::{FutureExt as _, StreamExt as _};
use reachability_core::Monitor;
use tracing::info;

mod eventloop;

use crate::eventloop::EventLoop;

#[fuchsia::main(logging_tags = ["reachability"])]
fn main() {
    // TODO(dpradilla): use a `StructOpt` to pass in a log level option where the user can control
    // how verbose logs should be.
    info!("Starting reachability monitor!");
    let mut executor =
        fuchsia_async::LocalExecutor::new().expect("failed to create local executor");

    let mut fs = ServiceFs::new_local();
    let mut fs = fs.take_and_serve_directory_handle().expect("failed to serve ServiceFS directory");

    let inspector = component::inspector();
    let () = inspect_runtime::serve(&inspector, &mut fs).expect("failed to serve inspect");

    let mut monitor = Monitor::new().expect("failed to create reachability monitor");
    let () = monitor.set_inspector(inspector);

    info!("monitoring");
    let mut eventloop = EventLoop::new(monitor);
    let eventloop_fut = eventloop.run().fuse();
    let serve_fut = fs.fuse().collect();
    futures::pin_mut!(eventloop_fut, serve_fut);

    executor.run_singlethreaded(async {
        futures::select! {
            r = eventloop_fut => {
                let r: Result<(), anyhow::Error> = r;
                panic!("unexpectedly exited event loop with result {:?}", r);
            },
            () = serve_fut => {
                panic!("unexpectedly stopped serving ServiceFS");
            },
        }
    })
}
