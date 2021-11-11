// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for Product Bundle Metadata (PBM).

use ffx_emulator_common::config::{FfxConfigWrapper, FMS_DATA_DIR, SDK_ROOT};
use ffx_emulator_config::EmulatorEngine;
use ffx_emulator_start_args::StartCommand;
use fms;
use std::path::PathBuf;

/// Apply values from the Product Bundle Metadata (PBM) to the StartCommand.
///
/// The PBM links to information about the virtual device specification which
/// will inform the emulator on what device to emulate.
pub async fn update_engine_with_pbm(
    start_command: &StartCommand,
    engine: &mut dyn EmulatorEngine,
    config: &FfxConfigWrapper,
) -> Result<(), anyhow::Error> {
    // TODO(fxbug.dev/88239): Move the PBM directory discovery out of the
    // emulator plugin.
    let mut fms_data_dir: PathBuf = config.file(FMS_DATA_DIR).await?;
    const SDK_ROOT_VAR: &str = "{sdk.root}/";
    if fms_data_dir.starts_with(SDK_ROOT_VAR) {
        let sdk_root: PathBuf = config.file(SDK_ROOT).await?;
        fms_data_dir = sdk_root.join(&fms_data_dir.strip_prefix(SDK_ROOT_VAR)?);
    }
    let fms_entries = fms::Entries::from_dir_path(&fms_data_dir)?;

    let product_bundle = fms::find_product_bundle(&fms_entries, &start_command.product_bundle)?;

    let virtual_device = fms::find_virtual_device(&fms_entries, &product_bundle.device_refs)?;
    println!(
        "Found PBM {:?}, device_refs {:?}, virtual_device {:?}.",
        product_bundle.name, product_bundle.device_refs, virtual_device,
    );

    engine.initialize(product_bundle, virtual_device)
}
