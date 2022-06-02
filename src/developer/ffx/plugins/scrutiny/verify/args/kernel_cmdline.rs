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
    description = "Verifies that kernel commandline arguments match golden files.",
    example = "To verify kernel cmdline arguments on your current build:

        $ ffx scrutiny verify kernel-cmdline \
            --zbi path/to/image.zbi \
            --golden path/to/golden"
)]
pub struct Command {
    /// path to ZBI image file that contains bootfs.
    #[argh(option)]
    pub zbi: PathBuf,
    /// path(s) to golden files to compare against during verification.
    #[argh(option)]
    pub golden: Vec<PathBuf>,
}
