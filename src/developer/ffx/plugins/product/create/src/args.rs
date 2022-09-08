// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

/// Create a Product Bundle using the outputs of Product Assembly.
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "create")]
pub struct CreateCommand {
    /// path to an assembly manifest, which specifies images to put in slot A.
    #[argh(option)]
    pub system_a: Option<PathBuf>,

    /// directory to write the product bundle.
    #[argh(option)]
    pub out_dir: PathBuf,
}
