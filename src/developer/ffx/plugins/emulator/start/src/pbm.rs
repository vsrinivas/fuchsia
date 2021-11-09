// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for Product Bundle Metadata (PBM).

use ffx_config;
use ffx_emulator_start_args::StartCommand;
use fms;
use std::path::PathBuf;

/// Apply values from the Product Bundle Metadata (PBM) to the StartCommand.
///
/// The PBM links to information about the virtual device specification which
/// will inform the emulator on what device to emulate.
pub async fn update_cmd_with_pbm(
    start_command: StartCommand,
) -> Result<StartCommand, anyhow::Error> {
    // TODO(fxbug.dev/88239): Move the PBM directory discovery out of the
    // emulator plugin.
    let mut fms_data_dir: PathBuf = ffx_config::get("sdk.fms.data.dir").await?;
    const SDK_ROOT: &str = "{sdk.root}/";
    if fms_data_dir.starts_with(SDK_ROOT) {
        let sdk_root: PathBuf = ffx_config::get("sdk.root").await?;
        fms_data_dir = sdk_root.join(&fms_data_dir.strip_prefix(SDK_ROOT)?);
    }
    let fms_entries = fms::Entries::from_dir_path(&fms_data_dir)?;

    let product_bundle = fms::find_product_bundle(&fms_entries, &start_command.product_bundle)?;

    // This should end with `)?;`, but since this currently always fails, the
    // `).ok();` to convert the Result to an Option is used.
    let virtual_device = fms::find_virtual_device(&fms_entries, &product_bundle.device_refs).ok();
    println!(
        "Found PBM {:?}, device_refs {:?}, virtual_device {:?}.",
        product_bundle.name, product_bundle.device_refs, virtual_device,
    );

    // TODO(fxbug.dev/87871): This is the place to change the start_command
    // based on the PBM and virtual device specification.

    Ok(start_command)
}
