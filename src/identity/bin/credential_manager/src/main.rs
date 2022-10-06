// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
mod credential_manager;
mod diagnostics;
mod hash_tree;
mod label_generator;
mod lookup_table;
mod pinweaver;
mod provision;

use {
    crate::{
        credential_manager::CredentialManager,
        diagnostics::{InspectDiagnostics, INSPECTOR},
        hash_tree::HashTreeStorageCbor,
        lookup_table::{PersistentLookupTable, LOOKUP_TABLE_PATH},
        pinweaver::PinWeaver,
        provision::provision,
    },
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_identity_credential::{ManagerRequestStream, ResetterRequestStream},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_tpm_cr50::PinWeaverMarker,
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    fuchsia_fs::directory::open_in_namespace,
    futures::StreamExt,
    std::sync::Arc,
    tracing::info,
};

/// The path where the hash tree is stored on disk.
pub const HASH_TREE_PATH: &str = "/data/hash_tree";

enum Services {
    CredentialManager(ManagerRequestStream),
    Resetter(ResetterRequestStream),
}

#[fuchsia::main(logging_tags = ["identity", "credential_manager"])]
async fn main() -> Result<(), Error> {
    info!("Starting credential manager");

    info!("Initializing diagnostics");
    let diagnostics = Arc::new(InspectDiagnostics::new(INSPECTOR.root()));

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

    let mut lookup_table = PersistentLookupTable::new(cred_data);
    let pinweaver = PinWeaver::new(pinweaver_proxy, Arc::clone(&diagnostics));
    let hash_tree_store = HashTreeStorageCbor::new(HASH_TREE_PATH, Arc::clone(&diagnostics));
    info!("Provisioning Hash Tree");
    let hash_tree = provision(&hash_tree_store, &mut lookup_table, &pinweaver)
        .await
        .map_err(|e| anyhow!("Provisioning failed: {:?}", e))?;
    info!("Creating handler for CredentialManager FIDL");
    let credential_manager =
        CredentialManager::new(pinweaver, hash_tree, lookup_table, hash_tree_store, diagnostics)
            .await;

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(Services::CredentialManager);
    fs.dir("svc").add_fidl_service(Services::Resetter);
    inspect_runtime::serve(&INSPECTOR, &mut fs)?;
    fs.take_and_serve_directory_handle().context("serving directory handle")?;
    // It is important that this remains `for_each` to create a sequential queue and prevent
    // subsequent requests being serviced before the first finishes.
    info!("Starting FIDL services");
    fs.for_each_concurrent(None, |service| async {
        match service {
            Services::CredentialManager(stream) => {
                credential_manager.handle_requests_for_stream(stream).await
            }
            Services::Resetter(stream) => {
                credential_manager.handle_requests_for_reset_stream(stream).await
            }
        }
    })
    .await;
    Ok(())
}
