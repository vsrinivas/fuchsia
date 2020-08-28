// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "128"]

use {
    anyhow::{format_err, Error},
    directory_broker,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fuchsia_async as fasync, fuchsia_runtime,
    fuchsia_syslog::fx_log_info,
    fuchsia_zircon as zx, io_util,
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path, pseudo_directory,
    },
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["storage_user"])?;
    fx_log_info!("storage_user is starting up");

    let out_dir_handle =
        fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleType::DirectoryRequest.into())
            .ok_or(format_err!("missing startup handle"))?;

    let out_dir = pseudo_directory! {
        "data" =>
            directory_broker::DirectoryBroker::new(Box::new(|flags, mode, path, server_end| {
                fx_log_info!("new handle to data directory being given out");
                let data_proxy = io_util::open_directory_in_namespace("/data", flags).unwrap();
                if path == "" {
                    data_proxy.clone(flags, server_end).unwrap()
                } else {
                    data_proxy.open(flags, mode, &path, server_end).unwrap()
                }
            })),
    };
    out_dir.open(
        ExecutionScope::new(),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        MODE_TYPE_DIRECTORY,
        path::Path::empty(),
        ServerEnd::new(zx::Channel::from(out_dir_handle)),
    );
    // Permanently hanging here because without this, storage_integration_test will hang due to
    // this function exiting early.
    fasync::futures::future::pending::<()>().await;
    Ok(())
}
