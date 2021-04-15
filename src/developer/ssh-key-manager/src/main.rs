// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::manager::SshKeyManager,
    anyhow::{Context, Error},
    fidl_fuchsia_ssh::AuthorizedKeysRequestStream,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::fx_log_err,
    futures::prelude::*,
};

mod keys;
mod manager;

enum Services {
    AuthorizedKeys(AuthorizedKeysRequestStream),
}

async fn run() -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let manager = SshKeyManager::new("/ssh/authorized_keys")?;

    fs.dir("svc").add_fidl_service(Services::AuthorizedKeys);
    fs.take_and_serve_directory_handle().context("serving directory handle")?;

    fs.for_each_concurrent(None, |item| match item {
        Services::AuthorizedKeys(stream) => manager
            .handle_requests(stream)
            .unwrap_or_else(|e| fx_log_err!("Failed to handle stream: {:?}", e)),
    })
    .await;
    Ok(())
}

#[fuchsia::component]
async fn main() {
    run().await.unwrap_or_else(|e| fx_log_err!("Failed to run ssh-key-manager: {:?}", e));
}
