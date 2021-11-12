// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for Product Bundle Metadata (PBM).

use ffx_emulator_start_args::StartCommand;
use fms;

/// Apply values from the Product Bundle Metadata (PBM) to the StartCommand.
///
/// The PBM links to information about the virtual device specification which
/// will inform the emulator on what device to emulate.
pub async fn update_engine_with_pbm(start_command: &StartCommand) -> Result<(), anyhow::Error> {
    let fms_entries = fms::Entries::from_config().await?;
    let product_bundle = fms::find_product_bundle(&fms_entries, &start_command.product_bundle)?;
    let virtual_device = fms::find_virtual_device(&fms_entries, &product_bundle.device_refs)?;
    println!(
        "Found PBM {:?}, device_refs {:?}, virtual_device {:?}.",
        product_bundle.name, product_bundle.device_refs, virtual_device,
    );

    // TODO: Get the values into the engine
    Ok(())
}
