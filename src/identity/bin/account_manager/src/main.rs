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
#![feature(async_await, await_macro)]

mod account_event_emitter;
mod account_handler_connection;
mod account_handler_context;
mod account_manager;
mod stored_account_list;

use crate::account_manager::AccountManager;
use failure::{Error, ResultExt};
use fidl_fuchsia_auth::AuthProviderConfig;
use fuchsia_async as fasync;
use fuchsia_component::fuchsia_single_component_package_url;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use lazy_static::lazy_static;
use log::{error, info};
use std::path::PathBuf;
use std::sync::Arc;

// This flag (prefixed with `--`) results in a set of hermetic auth providers.
const DEV_AUTH_PROVIDERS_FLAG: &str = "dev-auth-providers";

// Default data directory for the AccountManager.
const DATA_DIR: &str = "/data";

lazy_static! {
    /// (Temporary) Configuration for a fixed set of auth providers used until file-based
    /// configuration is available.
    static ref DEFAULT_AUTH_PROVIDER_CONFIG: Vec<AuthProviderConfig> = {
        vec![AuthProviderConfig {
            auth_provider_type: "google".to_string(),
            url: fuchsia_single_component_package_url!("google_auth_provider").to_string(),
            params: None
        }]
    };

    /// Configuration for a set of fake auth providers used for testing.
    static ref DEV_AUTH_PROVIDER_CONFIG: Vec<AuthProviderConfig> = {
        vec![AuthProviderConfig {
            auth_provider_type: "dev_auth_provider".to_string(),
            url: "fuchsia-pkg://fuchsia.com/dev_auth_provider#meta/dev_auth_provider.cmx"
            .to_string(),
            params: None
        }]
    };
}

fn main() -> Result<(), Error> {
    // Parse CLI args
    let mut opts = getopts::Options::new();
    opts.optflag(
        "",
        DEV_AUTH_PROVIDERS_FLAG,
        "use dev auth providers instead of the default set, for tests",
    );
    let args: Vec<String> = std::env::args().collect();
    let options = opts.parse(args)?;
    let auth_provider_config: &Vec<_> = if options.opt_present(DEV_AUTH_PROVIDERS_FLAG) {
        &DEV_AUTH_PROVIDER_CONFIG
    } else {
        &DEFAULT_AUTH_PROVIDER_CONFIG
    };

    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting account manager");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    let account_manager = AccountManager::new(PathBuf::from(DATA_DIR), &auth_provider_config)
        .map_err(|e| {
            error!("Error initializing AccountManager {:?}", e);
            e
        })?;
    let account_manager = Arc::new(account_manager);

    let mut fs = ServiceFs::new();
    fs.dir("public").add_fidl_service(move |stream| {
        let account_manager_clone = Arc::clone(&account_manager);
        fasync::spawn(
            async move {
                await!(account_manager_clone.handle_requests_from_stream(stream))
                    .unwrap_or_else(|e| error!("Error handling AccountManager channel {:?}", e))
            },
        );
    });
    fs.take_and_serve_directory_handle()?;

    executor.run_singlethreaded(fs.collect::<()>());
    info!("Stopping account manager");
    Ok(())
}
