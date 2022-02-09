// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "bootfs",
    description = "Verifies list of files in bootfs embedded in ZBI image against a golden file",
    example = "To verify bootfs on your current build:

        $ffx scrutiny verify bootfs --zbi path/to/image.zbi --golden path/to/bootfs_golden",
    note = "Verifies all file paths in bootfs."
)]
pub struct ScrutinyBootfsCommand {
    /// path to ZBI image file that contains bootfs.
    #[argh(option)]
    pub zbi: String,
    /// path(s) to golden file(s) for verifying bootfs paths.
    #[argh(option)]
    pub golden: Vec<String>,
    /// path to stamp file to write to if and only if verification succeeds.
    #[argh(option)]
    pub stamp: Option<String>,
}
