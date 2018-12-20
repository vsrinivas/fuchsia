// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! TokenManagerFactory manages service provider credentials for a set of users.
//!
//! The interaction with each service provider is mediated by a component implementing the
//! `AuthProvider` interface. These components are launched on demand. The set of configured
//! AuthProviders must be consistent across all calls to `TokenManagerFactory.GetTokenManager`.
//! The details of token management and implementation of the `TokenManager` interface are provided
//! by the `token_manager` crate.
//!
//! NOTE: Once `account_manager` provides token management for Fuchsia accounts this
//! `token_manager_factory` may be downscoped to only handle tokens that are independent of user.

#![deny(warnings)]
#![deny(missing_docs)]
#![feature(async_await, await_macro, futures_api)]

mod auth_context_supplier;
mod auth_provider_client;
mod auth_provider_supplier;
mod token_manager_factory;

use crate::token_manager_factory::TokenManagerFactory;
use failure::{Error, ResultExt};
use fidl::endpoints::RequestStream;
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_auth::{TokenManagerFactoryMarker, TokenManagerFactoryRequestStream};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use log::info;
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
                async move {
                    let stream = TokenManagerFactoryRequestStream::from_channel(chan);
                    await!(tmf_clone.handle_requests_from_stream(stream))
                },
            );
        }))
        .start()
        .context("Error starting token manager factory server")?;

    executor
        .run_singlethreaded(fut)
        .context("Failed to execute token manager factory future")?;
    info!("Stopping token manager factory");
    Ok(())
}
