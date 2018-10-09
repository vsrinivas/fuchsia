// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro, futures_api)]

mod account_manager;

use crate::account_manager::AccountManager;
use failure::{Error, ResultExt};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_auth_account::AccountManagerMarker;
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use log::info;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting account manager");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let fut = ServicesServer::new()
        .add_service((AccountManagerMarker::NAME, |chan| {
            AccountManager::spawn(chan)
        }))
        .start()
        .context("Error starting Auth AccountManager server")?;

    executor
        .run_singlethreaded(fut)
        .context("Failed to execute Auth AccountManager future")?;
    info!("Stopping account manager");
    Ok(())
}
