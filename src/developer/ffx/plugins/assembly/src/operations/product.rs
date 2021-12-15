// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config::product_config::ProductAssemblyConfig;
use crate::util;

use anyhow::Result;
use ffx_assembly_args::ProductArgs;
use log::info;

pub fn assemble(args: ProductArgs) -> Result<()> {
    let ProductArgs { product, outdir: _, gendir: _, input_bundles_dir: _ } = args;

    info!("Loading configuration files.");
    info!("  product: {}", product.display());

    let _product: ProductAssemblyConfig = util::read_config(&product)?;

    Ok(())
}
