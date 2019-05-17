// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![deny(warnings)]
#![allow(dead_code)]

use {
    failure::Error,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self, macros::*},
    futures::StreamExt,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["archivist"]).expect("can't init logger");
    fx_log_info!("Archivist is starting up...");

    // Serve no services, but create an outgoing "test" directory to see if hub is working.
    let mut fs = ServiceFs::new();
    fs.dir("test");
    fs.take_and_serve_directory_handle()?;

    await!(startup())?;

    await!(fs.collect::<()>());
    Ok(())
}

// TODO: Create an archive on startup, then start ticking (for garbage collection and rollover) and
// install directory watches in the correct places.
async fn startup() -> Result<(), Error> {
    Ok(())
}
