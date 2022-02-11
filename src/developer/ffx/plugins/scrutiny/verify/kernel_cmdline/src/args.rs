// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "kernel-cmdline",
    description = "Verifies that kernel commandline arguments match golden files.",
    example = "To verify kernel cmdline arguments on your current build:

        $ffx scrutiny verify kernel-cmdline --zbi path/to/image.zbi --golden path/to/golden"
)]
pub struct ScrutinyKernelCmdlineCommand {
    /// path to ZBI image to be verified.
    #[argh(option)]
    pub zbi: String,
    /// path(s) to golden files to compare against during verification.
    #[argh(option)]
    pub golden: Vec<String>,
    /// path to stamp file to write to if and only if verification succeeds.
    #[argh(option)]
    pub stamp: Option<String>,
}
