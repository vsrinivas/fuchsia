// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for Product Bundle Metadata (PBM).

use anyhow::Result;
use ffx_emulator_config::EmulatorConfiguration;
use ffx_emulator_start_args::StartCommand;
use fms;

/// Create a RuntimeConfiguration based on the command line args.
pub(crate) async fn make_configs(cmd: &StartCommand) -> Result<EmulatorConfiguration> {
    let emulator_configuration: EmulatorConfiguration = EmulatorConfiguration::default();

    let fms_entries = fms::Entries::from_config().await?;
    let product_bundle = fms::find_product_bundle(&fms_entries, &cmd.product_bundle)?;
    let virtual_device = fms::find_virtual_device(&fms_entries, &product_bundle.device_refs)?;
    println!(
        "Found PBM {:?}, device_refs {:?}, virtual_device {:?}.",
        product_bundle.name, product_bundle.device_refs, virtual_device,
    );

    // Map the product and device specifications to the Host,Device, and Guest configs

    // Apply command line options.

    Ok(emulator_configuration)
}
