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
    name = "kernel-cmdline",
    description = "Verifies that kernel cmdline arguments match golden files.",
    example = r#"To verify kernel cmdline arguments on your current build:

    $ ffx scrutiny verify kernel-cmdline \
        --product-bundle $(fx get-build-dir)/obj/build/images/fuchsia/product_bundle \
        --golden path/to/golden"#
)]
pub struct Command {
    /// absolute or working directory-relative path to a product bundle.
    #[argh(option)]
    pub product_bundle: PathBuf,
    /// absolute or working directory-relative path(s) to golden files to compare against during
    /// verification.
    #[argh(option)]
    pub golden: Vec<PathBuf>,
}
