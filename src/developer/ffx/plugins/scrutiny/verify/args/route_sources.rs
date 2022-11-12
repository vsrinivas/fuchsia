// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    std::path::PathBuf,
    {argh::FromArgs, ffx_core::ffx_command},
};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "route-sources",
    description = "Verifies that routes to designated components are routed from designated sources.",
    example = r#"To verify route sources according to a configuration file on your current build:

    $ ffx scrutiny verify route-sources
        --product-bundle $(fx get-build-dir)/obj/build/images/fuchsia/product_bundle \
        --config path/to/verify_route_sources/product.board.json5"#
)]
pub struct Command {
    /// absolute or working directory-relative path to a product bundle.
    #[argh(option)]
    pub product_bundle: PathBuf,
    /// absolute or working directory-relative path to configuration file that specifies components
    /// and their expected route sources.
    #[argh(option)]
    pub config: PathBuf,
}
