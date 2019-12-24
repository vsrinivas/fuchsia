// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod game;
mod tennis_service;

use anyhow::{Context, Error};
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{fx_log_info, init_with_tags};
use futures::StreamExt;

fn main() -> Result<(), Error> {
    init_with_tags(&["tennis_service"]).expect("tennis syslog init should not fail");
    fx_log_info!("tennis service started");
    let mut executor = fuchsia_async::Executor::new()
        .context("Creating fuchsia_async executor for tennis service failed")?;
    let tennis = tennis_service::TennisService::new();
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(|stream| tennis.bind(stream));
    fs.take_and_serve_directory_handle()?;
    let () = executor.run_singlethreaded(fs.collect());
    Ok(())
}
