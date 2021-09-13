// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug, Default)]
#[argh(
    subcommand,
    name = "build",
    description = "Builds a package.
"
)]
pub struct BuildCommand {
    #[argh(option, description = "path to the build manifest file")]
    pub build_manifest_path: PathBuf,

    #[argh(option, description = "write package manifest to this path")]
    pub package_manifest_path: Option<PathBuf>,

    #[argh(
        option,
        short = 'o',
        default = "PathBuf::from(\"./out\")",
        description = "directory to save package artifacts"
    )]
    pub out: PathBuf,

    #[argh(option, description = "name of the package")]
    pub published_name: String,
}
