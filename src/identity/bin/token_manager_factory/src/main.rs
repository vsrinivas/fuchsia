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

#![deny(missing_docs)]

mod auth_provider_supplier;
mod token_manager_factory;

use crate::token_manager_factory::TokenManagerFactory;
use anyhow::{Context as _, Error};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use log::info;
use std::sync::Arc;

// Default data directory for the TokenManagerFactory.
const DATA_DIR: &str = "/data";

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");

    // Create a single token manager factory instance that we use to back all incoming requests.
    info!("Starting token manager factory");
    let token_manager_factory = Arc::new(TokenManagerFactory::new(DATA_DIR.into()));

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        let tmf_clone = Arc::clone(&token_manager_factory);
        fasync::spawn(async move { tmf_clone.handle_requests_from_stream(stream).await });
    });
    fs.take_and_serve_directory_handle()?;

    executor.run_singlethreaded(fs.collect::<()>());
    info!("Stopping token manager factory");
    Ok(())
}
