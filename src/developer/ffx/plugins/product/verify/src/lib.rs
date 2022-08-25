// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Verifier methods that ensure that a downloaded product bundle is a valid and ready to use
//! format.
//! TODO(fxbug.dev/106850): Investigate whether using the validator Rust crate helps simplify this
//! logic.

use anyhow::{Context, Result};
use ffx_core::ffx_plugin;
use ffx_product_verify_args::VerifyCommand;
use sdk_metadata::{
    product_bundle_validate, Envelope, PhysicalDeviceV1, ProductBundleV1, VirtualDeviceV1,
};
use std::fs::{self, File};

/// Verify that the product bundle has the correct format and is ready for use.
#[ffx_plugin("product.experimental")]
fn pb_verify(cmd: VerifyCommand) -> Result<()> {
    if let Some(product_bundle) = &cmd.product_bundle {
        let file = File::open(product_bundle).context("opening product bundle")?;
        let envelope: Envelope<ProductBundleV1> =
            serde_json::from_reader(file).context("parsing product bundle")?;
        product_bundle_validate(envelope.data)?;
    }
    if let Some(virtual_device) = &cmd.virtual_device {
        let file = File::open(virtual_device).context("opening virtual device")?;
        let _: Envelope<VirtualDeviceV1> =
            serde_json::from_reader(file).context("parsing virtual device")?;
        // If serde can deserialize the virtual device, then it is valid.
    }
    if let Some(physical_device) = &cmd.physical_device {
        let file = File::open(physical_device).context("opening physical device")?;
        let _: Envelope<PhysicalDeviceV1> =
            serde_json::from_reader(file).context("parsing physical_device")?;
        // If serde can deserialize the physical device, then it is valid.
    }
    if let Some(verified_path) = &cmd.verified_file {
        fs::write(verified_path, "verified").context("writing verified file")?;
    }
    Ok(())
}
