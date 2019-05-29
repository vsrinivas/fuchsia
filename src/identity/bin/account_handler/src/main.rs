// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! AccountHandler manages the state of a single Fuchsia account and its personae on a Fuchsia
//! device, and provides access to authentication tokens for Service Provider accounts associated
//! with the Fuchsia account.

#![deny(warnings)]
#![deny(missing_docs)]
#![feature(async_await, await_macro, result_map_or_else)]

mod account;
mod account_handler;
mod auth_provider_supplier;
mod persona;
mod stored_account;

#[cfg(test)]
mod test_util;

use crate::account_handler::AccountHandler;
use failure::{Error, ResultExt};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use log::{error, info};
use std::sync::Arc;

type TokenManager = token_manager::TokenManager<auth_provider_supplier::AuthProviderSupplier>;

const DATA_DIR: &str = "/data";

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting account handler");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let account_handler = Arc::new(AccountHandler::new(DATA_DIR.into()));

    let mut fs = ServiceFs::new();
    fs.dir("public").add_fidl_service(move |stream| {
        let account_handler_clone = Arc::clone(&account_handler);
        fasync::spawn(async move {
            await!(account_handler_clone.handle_requests_from_stream(stream))
                .unwrap_or_else(|e| error!("Error handling AccountHandlerControl channel {:?}", e))
        });
    });
    fs.take_and_serve_directory_handle()?;

    executor.run_singlethreaded(fs.collect::<()>());

    info!("Stopping account handler");
    Ok(())
}
