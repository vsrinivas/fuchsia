// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! AccountManager manages the overall state of Fuchsia accounts and personae on
//! a Fuchsia device, installation of the AuthProviders that are used to obtain
//! authentication tokens for these accounts, and access to TokenManagers for
//! these accounts.
//!
//! The AccountManager is the most powerful interface in the authentication
//! system and is intended only for use by the most trusted parts of the system.

#![deny(warnings)]
#![deny(missing_docs)]
#![feature(async_await, await_macro, futures_api)]

mod account_event_emitter;
mod account_handler_connection;
mod account_handler_context;
mod account_manager;
mod stored_account_list;

use crate::account_manager::AccountManager;
use failure::{Error, ResultExt};
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fuchsia_auth_account::{AccountManagerMarker, AccountManagerRequestStream};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use log::{error, info};
use std::path::PathBuf;
use std::sync::Arc;

// Default accounts parent directory, where individual accounts are stored.
const ACCOUNT_DIR_PARENT: &str = "/data/account";

// Default data directory for the AccountManager.
const DATA_DIR: &str = "/data";

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting account manager");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    let account_manager = AccountManager::new(ACCOUNT_DIR_PARENT, PathBuf::from(DATA_DIR))
        .map_err(|e| {
            error!("Error initializing AccountManager {:?}", e);
            e
        })?;
    let account_manager = Arc::new(account_manager);

    let fut = ServicesServer::new()
        .add_service((AccountManagerMarker::NAME, move |chan| {
            let account_manager_clone = Arc::clone(&account_manager);
            fasync::spawn(
                async move {
                    let stream = AccountManagerRequestStream::from_channel(chan);
                    await!(account_manager_clone.handle_requests_from_stream(stream))
                        .unwrap_or_else(|e| error!("Error handling AccountManager channel {:?}", e))
                },
            );
        }))
        .start()
        .context("Error starting AccountManager server")?;

    executor.run_singlethreaded(fut).context("Failed to execute AccountManager future")?;
    info!("Stopping account manager");
    Ok(())
}
