// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! AccountHandler manages the state of a single Fuchsia account and its personae on a Fuchsia
//! device, and provides access to authentication tokens for Service Provider accounts associated
//! with the Fuchsia account.

#![deny(missing_docs)]

mod account;
mod account_handler;
mod auth_provider_supplier;
mod common;
mod inspect;
mod persona;
mod stored_account;

#[cfg(test)]
mod test_util;

use crate::account_handler::AccountHandler;
use crate::common::AccountLifetime;
use failure::{Error, ResultExt};
use fidl_fuchsia_identity_internal::AccountHandlerContextMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect::Inspector;
use futures::StreamExt;
use log::{error, info};
use std::sync::Arc;

type TokenManager = token_manager::TokenManager<auth_provider_supplier::AuthProviderSupplier>;

const DATA_DIR: &str = "/data";

/// This flag (prefixed with `--`) results in an in-memory ephemeral account.
const EPHEMERAL_FLAG: &str = "ephemeral";

fn main() -> Result<(), Error> {
    let mut opts = getopts::Options::new();
    opts.optflag("", EPHEMERAL_FLAG, "this account is an in-memory ephemeral account");
    let args: Vec<String> = std::env::args().collect();
    let options = opts.parse(args)?;
    let lifetime = if options.opt_present(EPHEMERAL_FLAG) {
        AccountLifetime::Ephemeral
    } else {
        AccountLifetime::Persistent { account_dir: DATA_DIR.into() }
    };

    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting account handler");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let inspector = Inspector::new();
    let mut fs = ServiceFs::new();
    inspector.serve(&mut fs)?;

    // TODO(dnordstrom): Find a testable way to inject global capabilities.
    let context = connect_to_service::<AccountHandlerContextMarker>()
        .expect("Error connecting to the AccountHandlerContext service");
    let account_handler = Arc::new(AccountHandler::new(context, lifetime, &inspector));
    fs.dir("svc").add_fidl_service(move |stream| {
        let account_handler_clone = Arc::clone(&account_handler);
        fasync::spawn(async move {
            account_handler_clone
                .handle_requests_from_stream(stream)
                .await
                .unwrap_or_else(|e| error!("Error handling AccountHandlerControl channel {:?}", e))
        });
    });
    fs.take_and_serve_directory_handle()?;

    executor.run_singlethreaded(fs.collect::<()>());

    info!("Stopping account handler");
    Ok(())
}
