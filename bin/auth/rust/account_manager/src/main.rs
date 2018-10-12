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

mod account_manager;

use crate::account_manager::AccountManager;
use failure::{Error, ResultExt};
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fuchsia_auth_account::{AccountManagerMarker, AccountManagerRequestStream};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use futures::prelude::*;
use log::{error, info};
use std::sync::{Arc, Mutex};

fn spawn_from_channel(account_manager: Arc<Mutex<AccountManager>>, chan: fasync::Channel) {
    fasync::spawn(
        AccountManagerRequestStream::from_channel(chan)
            .try_for_each(move |req| account_manager.lock().unwrap().handle_request(req))
            .unwrap_or_else(|e| error!("Error handling AccountManager channel {:?}", e)),
    );
}

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting account manager");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let account_manager = Arc::new(Mutex::new(AccountManager::new()));

    let fut = ServicesServer::new()
        .add_service((AccountManagerMarker::NAME, move |chan| {
            spawn_from_channel(account_manager.clone(), chan)
        }))
        .start()
        .context("Error starting Auth AccountManager server")?;

    executor
        .run_singlethreaded(fut)
        .context("Failed to execute Auth AccountManager future")?;
    info!("Stopping account manager");
    Ok(())
}
