// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_assembly_args::ProductSizeCheckArgs;

/// Verifies that the product budget is not exceeded.
pub fn verify_product_budgets(_args: ProductSizeCheckArgs) -> Result<()> {
    unimplemented!();
}
