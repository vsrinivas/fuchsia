// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
mod credential_manager;
mod hash_tree;
mod label_generator;
mod lookup_table;
mod pinweaver;

use anyhow::{Context, Error};
use fidl_fuchsia_identity_credential::CredentialManagerRequestStream;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_tpm_cr50::PinWeaverMarker;
use fuchsia_async as fasync;
use fuchsia_component::{client::connect_to_protocol, server::ServiceFs};
use futures::StreamExt;
use io_util::directory::open_in_namespace;
use log::info;

use crate::{
    credential_manager::CredentialManager,
    lookup_table::{PersistentLookupTable, LOOKUP_TABLE_PATH},
    pinweaver::PinWeaver,
};

/// The path where the hash tree is stored on disk.
pub const HASH_TREE_PATH: &str = "/data/hash_tree";

enum Services {
    CredentialManager(CredentialManagerRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting credential manager");

    info!("Connecting to cr50_agent PinWeaver protocol");
    let pinweaver_proxy = connect_to_protocol::<PinWeaverMarker>()
        .context("Failed to connect to cr50_agent PinWeaver protocol")?;

    info!("Reading Persistent Lookup Table");
    let cred_data = open_in_namespace(
        LOOKUP_TABLE_PATH,
        fio::OpenFlags::RIGHT_READABLE
            | fio::OpenFlags::RIGHT_WRITABLE
            | fio::OpenFlags::DIRECTORY
            | fio::OpenFlags::CREATE,
    )?;
    let lookup_table = PersistentLookupTable::new(cred_data);
    let pinweaver = PinWeaver::new(pinweaver_proxy);
    let credential_manager = CredentialManager::new(HASH_TREE_PATH, pinweaver, lookup_table)
        .await
        .expect("failed to provision credential manager");

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(Services::CredentialManager);
    fs.take_and_serve_directory_handle().context("serving directory handle")?;
    // It is important that this remains `for_each` to create a sequential queue and prevent
    // subsequent requests being serviced before the first finishes.
    fs.for_each(|service| match service {
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
