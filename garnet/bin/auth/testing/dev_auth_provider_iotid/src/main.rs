// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro, futures_api)]

mod dev_auth_provider_iotid;
mod dev_auth_provider_iotid_factory;

use crate::dev_auth_provider_iotid_factory::AuthProviderFactory;
use failure::{Error, ResultExt};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_auth::AuthProviderFactoryMarker;
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use log::info;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting dev auth provider");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    let fut = ServicesServer::new()
        .add_service((AuthProviderFactoryMarker::NAME, |chan| {
            AuthProviderFactory::spawn(chan)
        }))
        .start()
        .context("Error starting dev auth provider server")?;

    executor
        .run_singlethreaded(fut)
        .context("Failed to execute dev auth provider future")?;
    Ok(())
}
