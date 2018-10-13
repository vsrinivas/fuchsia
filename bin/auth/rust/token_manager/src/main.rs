// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! TokenManager manages a set of Service Provider credentials for a Fuchsia account.
//!
//! The interaction with each service provider is mediated by a component implementing the
//! AuthProvider interface. Availalle AuthProviders are configured as a TokenManger is constructed.
//! The token manager retains long lived credentials such as OAuth refresh tokens in persistent
//! storage. These long lived credentials are used to request short lived credentials such as OAuth
//! access tokens that are cached in memory.

#![deny(warnings)]
#![deny(missing_docs)]
#![feature(async_await, await_macro, futures_api, pin)]

#[macro_use]
mod macros;
mod auth_context_client;
mod auth_provider_client;
mod error;
mod token_manager;
mod token_manager_factory;

use crate::token_manager_factory::TokenManagerFactory;
use failure::{Error, ResultExt};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_auth::TokenManagerFactoryMarker;
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use log::info;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting token manager");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
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
