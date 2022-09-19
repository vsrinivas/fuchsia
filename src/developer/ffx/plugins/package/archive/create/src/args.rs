// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "create",
    description = "create a package archive from a package_manifest.json"
)]
pub struct CreateCommand {
    #[argh(positional, description = "package_manifest.json to archive")]
    pub package_manifest: PathBuf,

    #[argh(option, description = "output package archive", short = 'o')]
    pub output: PathBuf,

    #[argh(
        option,
        description = "root directory for paths in package_manifest.json",
        default = "PathBuf::from(\".\")",
        short = 'r'
    )]
    pub root_dir: PathBuf,
}
