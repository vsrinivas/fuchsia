// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "components",
    description = "Lists all the components in the build",
    example = "To list all the components in the build:

        $ffx scrutiny list components ",
    note = "Lists all the components in the build in a json format."
)]
pub struct ScrutinyComponentsCommand {
    /// path to the build directory.
    #[argh(option)]
    pub build_path: PathBuf,
}
