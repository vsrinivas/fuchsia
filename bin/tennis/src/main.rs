// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

mod game;
mod tennis_service;

use failure::{Error, ResultExt};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_game_tennis::TennisServiceMarker;
use fuchsia_app::server::ServicesServer;
use fuchsia_syslog::{fx_log_info, init_with_tags};

fn main() -> Result<(), Error> {
    init_with_tags(&["tennis_service"]).expect("tennis syslog init should not fail");
    fx_log_info!("tennis service started");
    let mut executor = fuchsia_async::Executor::new()
        .context("Creating fuchsia_async executor for tennis service failed")?;
    let tennis = tennis_service::TennisService::new();
    let done = ServicesServer::new()
        .add_service((TennisServiceMarker::NAME, move |chan| {
            tennis.bind(chan);
        })).start()
        .context("Creating ServicesServer for tennis service failed")?;
    executor
        .run_singlethreaded(done)
        .context("Attempt to start up tennis services on async::Executor failed")?;

    Ok(())
}
