// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use fuchsia_url::AbsolutePackageUrl;
use std::path::PathBuf;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "package",
    description = "Lists all the files in a package",
    example = "To list all the files in a package:

        $ ffx scrutiny list package \
            --product-bundle $(fx get-build-dir)/obj/build/images/fuchsia/product_bundle \
            --url fuchsia-pkg://fuchsia.com/foo",
    note = "Lists all the package contents in json format."
)]
pub struct ScrutinyPackageCommand {
    /// path to a product bundle.
    #[argh(option)]
    pub product_bundle: PathBuf,
    /// fuchsia url to the package.
    #[argh(option)]
    pub url: AbsolutePackageUrl,
}
