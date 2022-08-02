// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! AccountHandler manages the state of a single system account and its personae on a Fuchsia
//! device, and provides access to authentication tokens for Service Provider accounts associated
//! with the account.

#![deny(missing_docs)]

mod account;
mod account_handler;
mod common;
mod inspect;
mod lock_request;
mod persona;
mod pre_auth;
mod stored_account;

#[cfg(test)]
mod test_util;

use {
    crate::{account_handler::AccountHandler, common::AccountLifetime},
    account_common::{AccountId, AccountManagerError},
    anyhow::{Context as _, Error},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::Inspector,
    futures::StreamExt,
    log::{error, info},
    std::sync::Arc,
};

const DATA_DIR: &str = "/data";

/// This command line flag (prefixed with `--`) results in an in-memory ephemeral account.
const EPHEMERAL_FLAG: &str = "ephemeral";

// TODO(dnordstrom): Remove all panics.
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

    // TODO(fxbug.dev/104516): We'll clean up account id when we deprecate stash.
    // Account ID was used to differentiate the different
    // stash stores in CFv2. But since we have sandboxed storage available in v2,
    // we don't need unique names for each. Hence the hardcoded value for now.
    let account_id = AccountId::new(111222);

    fuchsia_syslog::init_with_tags(&[
        "auth",
        &format!("id<{}>", &account_id.to_canonical_string()),
    ])
    .expect("Can't init logger");
    info!("Starting account handler");

    let mut executor = fasync::LocalExecutor::new().context("Error creating executor")?;
    let inspector = Inspector::new();
    let mut fs = ServiceFs::new();
    inspect_runtime::serve(&inspector, &mut fs)?;

    let pre_auth_manager = create_pre_auth_manager(&lifetime, &account_id)?;
    let account_handler =
        Arc::new(AccountHandler::new(account_id, lifetime, pre_auth_manager, &inspector));
    fs.dir("svc").add_fidl_service(move |stream| {
        let account_handler_clone = Arc::clone(&account_handler);
        fasync::Task::spawn(async move {
            account_handler_clone
                .handle_requests_from_stream(stream)
                .await
                .unwrap_or_else(|e| error!("Error handling AccountHandlerControl channel: {:?}", e))
        })
        .detach();
    });
    fs.take_and_serve_directory_handle()?;

    executor.run_singlethreaded(fs.collect::<()>());

    info!("Stopping account handler");
    Ok(())
}

/// Returns a pre-auth manager given the current lifetime and account id.
fn create_pre_auth_manager(
    lifetime: &AccountLifetime,
    id: &AccountId,
) -> Result<Arc<dyn pre_auth::Manager>, AccountManagerError> {
    if lifetime == &AccountLifetime::Ephemeral {
        Ok(Arc::new(pre_auth::InMemoryManager::create(pre_auth::State::NoEnrollments)))
    } else {
        let store_name = format!("account_handler/{}", &id.to_canonical_string());
        Ok(Arc::new(pre_auth::StashManager::create(&store_name)?))
    }
}
