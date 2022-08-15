// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

/// Verify that the contents of the product bundle are valid and ready for use.
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "verify")]
pub struct VerifyCommand {
    /// local directory containing the product bundle.
    #[argh(positional)]
    pub product_bundle: PathBuf,

    /// optional verified file to write after successfully verifying,
    /// so that build systems have an output to watch.
    #[argh(option)]
    pub verified_file: Option<PathBuf>,
}
