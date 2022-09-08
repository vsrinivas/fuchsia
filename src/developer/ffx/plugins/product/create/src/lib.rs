// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! FFX plugin for constructing product bundles, which are distributable containers for a product's
//! images and packages, and can be used to emulate, flash, or update a product.

use anyhow::{Context, Result};
use ffx_core::ffx_plugin;
use ffx_product_create_args::CreateCommand;
use sdk_metadata::{ProductBundle, ProductBundleV2, VersionedProductBundle};
use std::fs::File;

/// Create a product bundle.
#[ffx_plugin("product.experimental")]
fn pb_create(cmd: CreateCommand) -> Result<()> {
    let product_bundle = ProductBundle::V2(VersionedProductBundle::V2(ProductBundleV2 {
        name: "product.board".into(),
    }));
    let product_bundle_file = File::create(cmd.out_dir.join("product_bundle.json"))
        .context("creating product_bundle.json file")?;
    serde_json::to_writer(product_bundle_file, &product_bundle)
        .context("writing product_bundle.json file")?;
    Ok(())
}

#[cfg(test)]
mod test {}
