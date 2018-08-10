// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api)]

extern crate auth_cache;
extern crate auth_store;
#[macro_use]
extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_auth;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;
extern crate fuchsia_syslog as syslog;
extern crate fuchsia_zircon as zx;
extern crate futures;
#[macro_use]
extern crate log;

#[macro_use]
mod macros;
mod auth_context_client;
mod auth_provider_client;
mod error;
mod token_manager;
mod token_manager_factory;

use component::server::ServicesServer;
use failure::{Error, ResultExt};
use fidl::endpoints2::ServiceMarker;
use fidl_fuchsia_auth::TokenManagerFactoryMarker;
use token_manager_factory::TokenManagerFactory;

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting token manager");

    let mut executor = async::Executor::new().context("Error creating executor")?;
    let fut = ServicesServer::new()
        .add_service((TokenManagerFactoryMarker::NAME, |chan| {
            TokenManagerFactory::spawn(chan)
        }))
        .start()
        .context("Error starting Auth TokenManager server")?;

    executor
        .run_singlethreaded(fut)
        .context("Failed to execute Auth TokenManager future")?;
    info!("Stopping token manager");
    Ok(())
}
