// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_core::ffx_plugin, ffx_product_bundles_args::ProductBundlesCommand};

/// Provide functionality to list product-bundle metadata, fetch metadata, and
/// pull images and related data.
#[ffx_plugin("product-bundles.experimental")]
pub async fn exec_product_bundles(_command: ProductBundlesCommand) -> Result<()> {
    Ok(())
}

#[cfg(test)]
mod test {}
