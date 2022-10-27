// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! AccountHandler manages the state of a single system account and its personae on a Fuchsia
//! device, and provides access to authentication tokens for Service Provider accounts associated
//! with the account.

#![deny(missing_docs)]
#![warn(clippy::all)]
#![allow(clippy::enum_variant_names)]
#![allow(clippy::let_unit_value)]
#![allow(clippy::await_holding_refcell_ref)]
#![allow(clippy::from_over_into)]

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
    account_common::AccountId,
    anyhow::{Context as _, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_process_lifecycle::LifecycleRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_fs::directory::open_in_namespace,
    fuchsia_inspect::Inspector,
    fuchsia_runtime::{self as fruntime, HandleInfo, HandleType},
    futures::StreamExt,
    std::sync::Arc,
    storage_manager::{
        minfs::{disk::DevDiskManager, StorageManager as MinfsStorageManager},
        StorageManager,
    },
    tracing::{error, info},
};

const DATA_DIR: &str = "/data";

/// This command line flag (prefixed with `--`) results in an in-memory ephemeral account.
const EPHEMERAL_FLAG: &str = "ephemeral";

fn set_up_lifecycle_watcher<SM>(account_handler: Arc<AccountHandler<SM>>) -> fasync::Task<()>
where
    SM: StorageManager<Key = [u8; 32]> + Send + Sync + 'static,
{
    let handle_info = HandleInfo::new(HandleType::Lifecycle, 0);
    let handle = fruntime::take_startup_handle(handle_info)
        .expect("must have been provided a lifecycle channel in procargs");
    let async_chan =
        fasync::Channel::from_channel(handle.into()).expect("Async channel conversion failed.");
    let req_stream = LifecycleRequestStream::from_channel(async_chan);

    fasync::Task::spawn(
        async move { account_handler.handle_requests_for_lifecycle(req_stream).await },
    )
}

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

    let mut executor = fasync::LocalExecutor::new().context("Error creating executor")?;

    diagnostics_log::init!(&["identity", &format!("id<{}>", &account_id.to_canonical_string())]);
    info!("Starting account handler");

    let inspector = Inspector::new();
    let mut fs = ServiceFs::new();
    inspect_runtime::serve(&inspector, &mut fs)?;

    let account_handler = Arc::new(AccountHandler::new(
        account_id,
        lifetime,
        &inspector,
        /*storage_manager_factory=*/
        MinfsStorageManager::new(DevDiskManager::new(
            open_in_namespace(
                "/dev",
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            )
            .expect("open /dev root for disk manager"),
        )),
    ));

    let _lifecycle_task: fuchsia_async::Task<()> =
        set_up_lifecycle_watcher(Arc::clone(&account_handler));

    fs.dir("svc").add_fidl_service(move |stream| {
        let account_handler_clone = Arc::clone(&account_handler);
        fasync::Task::spawn(async move {
            account_handler_clone
                .handle_requests_from_stream(stream)
                .await
                .unwrap_or_else(|err| error!(?err, "Error handling AccountHandlerControl channel"))
        })
        .detach();
    });
    fs.take_and_serve_directory_handle()?;

    executor.run_singlethreaded(fs.collect::<()>());

    info!("Stopping account handler");
    Ok(())
}
