// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! AccountManager manages the overall state of system accounts and personae on
//! a Fuchsia device.
//!
//! The AccountManager is the most powerful interface in the authentication
//! system and is intended only for use by the most trusted parts of the system.

#![deny(missing_docs)]

mod account_event_emitter;
mod account_handler_connection;
mod account_manager;
mod account_map;
mod fake_account_handler_connection;
pub mod inspect;
mod stored_account_list;

use {
    crate::{
        account_handler_connection::AccountHandlerConnectionImpl, account_manager::AccountManager,
    },
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::Inspector,
    futures::prelude::*,
    std::{path::PathBuf, sync::Arc},
    tracing::{error, info},
};

/// Default data directory for the AccountManager.
const DATA_DIR: &str = "/data";

#[fuchsia::main(logging_tags = ["identity", "account_manager"])]
async fn main() -> Result<(), Error> {
    info!("Starting account manager");

    let mut fs = ServiceFs::new();
    let inspector = Inspector::new();
    inspect_runtime::serve(&inspector, &mut fs)?;

    let account_manager = Arc::new(
        AccountManager::<AccountHandlerConnectionImpl>::new(PathBuf::from(DATA_DIR), &inspector)
            .map_err(|err| {
                error!(?err, "Error initializing AccountManager");
                err
            })?,
    );

    fs.dir("svc").add_fidl_service(move |stream| {
        let account_manager_clone = Arc::clone(&account_manager);
        fasync::Task::spawn(async move {
            account_manager_clone
                .handle_requests_from_stream(stream)
                .await
                .unwrap_or_else(|err| error!(?err, "Error handling AccountManager channel"))
        })
        .detach();
    });

    fs.take_and_serve_directory_handle()?;

    fs.collect::<()>().await;
    info!("Stopping account manager");
    Ok(())
}
