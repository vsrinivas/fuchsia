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
    name = "archive",
    description = "archive a package_manifest.json into package archive"
)]
pub struct ArchiveCommand {
    #[argh(positional, description = "package_manifest.json to archive")]
    pub package_manifest: PathBuf,

    #[argh(option, description = "output", short = 'o')]
    pub output: PathBuf,

    #[argh(
        option,
        description = "build directory for package_manifest.json",
        default = "PathBuf::from(\".\")",
        short = 'b'
    )]
    pub build_dir: PathBuf,
}
