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
#![feature(async_await, await_macro, futures_api)]

mod auth_context_client;
mod auth_provider_client;
mod error;
mod token_manager;
mod token_manager_factory;

use crate::token_manager_factory::TokenManagerFactory;
use failure::{Error, ResultExt};
use fidl::endpoints::RequestStream;
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_auth::{TokenManagerFactoryMarker, TokenManagerFactoryRequestStream};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use futures::prelude::*;
use log::{error, info};
use std::sync::Arc;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");

    // Create a single token manager factory instance that we use to back all incoming requests.
    info!("Starting token manager factory");
    let token_manager_factory = Arc::new(TokenManagerFactory::new());

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let fut = ServicesServer::new()
        .add_service((TokenManagerFactoryMarker::NAME, move |chan| {
            let tmf_clone = Arc::clone(&token_manager_factory);
            fasync::spawn(
                (async move {
                    let mut stream = TokenManagerFactoryRequestStream::from_channel(chan);
                    while let Some(req) = await!(stream.try_next())? {
                        await!(tmf_clone.handle_request(req))?;
                    }
                    Ok(())
                })
                    .unwrap_or_else(|e: failure::Error| {
                        error!("Error handling TokenManagerFactory channel {:?}", e)
                    }),
            )
        }))
        .start()
        .context("Error starting token manager factory server")?;

    executor
        .run_singlethreaded(fut)
        .context("Failed to execute token manager factory future")?;
    info!("Stopping token manager factory");
    Ok(())
}
