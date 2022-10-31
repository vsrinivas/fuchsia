// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fdr_lib::execute_reset;
use fidl::endpoints::create_proxy;
use fidl_fuchsia_fshost::AdminMarker;
use fidl_fuchsia_io as fio;
use fuchsia_zircon as zx;

// Wipe and re-provision FVM. This will wipe both data and blobfs. This should only
// be used in OTA flows, since wiping blobfs will result in an unusable primary system.
// TODO(b/253096159): Add integration test for wipe_storage
pub async fn wipe_storage() -> Result<fio::DirectoryProxy, Error> {
    let fshost_admin = fuchsia_component::client::connect_to_protocol::<AdminMarker>()
        .context("Connecting to fshost Admin service.")?;

    let (blobfs_client, blobfs_server) = create_proxy::<fio::DirectoryMarker>().unwrap();
    fshost_admin
        .wipe_storage(blobfs_server)
        .await
        .context("Wiping storage")?
        .map_err(zx::Status::from_raw)?;

    Ok(blobfs_client)
}

// Instead of formatting the data partition directly, reset it via the factory reset service.
// The data partition is reformatted on first boot under normal circumstances and will do so
// after a reboot following being reset.
// This immediately reboots the device and needs to run separately from wipe_storage for now.
pub async fn wipe_data() -> Result<(), Error> {
    execute_reset().await.context("Failed to factory reset data")
}
