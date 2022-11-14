// Copyright 2021 The Fuchsia Authors. All rights reserved.
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
    name = "static-pkgs",
    description = "Check the static packages extracted from the ZBI against golden files",
    example = "To verify static packages on your current build:

        $ ffx scrutiny verify static-pkgs \
            --product-bundle $(fx get-build-dir)/obj/build/images/fuchsia/product_bundle \
            --golden path/to/golden"
)]
pub struct Command {
    /// path to a product bundle.
    #[argh(option)]
    pub product_bundle: PathBuf,
    /// path(s) to golden file(s) used to verify routes.
    #[argh(option)]
    pub golden: Vec<PathBuf>,
}
