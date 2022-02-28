// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::operations::product::assembly_builder::ImageAssemblyConfigBuilder;
use crate::util;
use anyhow::{Context, Result};
use assembly_config::product_config::ProductAssemblyConfig;
use ffx_assembly_args::ProductArgs;
use log::info;

mod assembly_builder;

pub fn assemble(args: ProductArgs) -> Result<()> {
    let ProductArgs { product, outdir, gendir: _, input_bundles_dir } = args;

    info!("Loading configuration files.");
    info!("  product: {}", product.display());

    let _product: ProductAssemblyConfig =
        util::read_config(&product).context("Loading product configuration")?;

    let mut builder = ImageAssemblyConfigBuilder::default();

    let legacy_bundle_path = input_bundles_dir.join("legacy").join("assembly_config.json");
    for bundle_path in vec![legacy_bundle_path] {
        builder
            .add_bundle(&bundle_path)
            .context(format!("Adding input bundle: {}", bundle_path.display()))?;
    }

    let image_assembly = builder.build(&outdir).context("Building Image Assembly config")?;
    assembly_validate_product::validate_product(&image_assembly)?;

    let image_assembly_path = outdir.join("image_assembly.json");
    let image_assembly_file = std::fs::File::create(&image_assembly_path).context(format!(
        "Failed to create image assembly config file: {}",
        image_assembly_path.display()
    ))?;
    serde_json::to_writer_pretty(image_assembly_file, &image_assembly)?;

    Ok(())
}
