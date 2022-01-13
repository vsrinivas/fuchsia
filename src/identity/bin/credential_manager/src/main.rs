// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
mod credential_manager;
mod hash_tree;
mod label_generator;
mod lookup_table;

use anyhow::{Context, Error};
use fidl_fuchsia_identity_credential::CredentialManagerRequestStream;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use log::info;

use crate::credential_manager::CredentialManager;

enum Services {
    CredentialManager(CredentialManagerRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting credential manager");

    // TODO(arkay): Ensure that we cleanup stale staged files and old versions
    // on initialization.
    let credential_manager = CredentialManager::new();
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(Services::CredentialManager);
    fs.take_and_serve_directory_handle().context("serving directory handle")?;

    fs.for_each_concurrent(None, |service| match service {
        Services::CredentialManager(stream) => {
            credential_manager.handle_requests_for_stream(stream)
        }
    })
    .await;

    Ok(())
}

#[cfg(test)]
mod test {
    // Add tests once we have behavior to test.
}
