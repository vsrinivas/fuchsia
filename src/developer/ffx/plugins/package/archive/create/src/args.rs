// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use camino::Utf8PathBuf;
use ffx_core::ffx_command;

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "create",
    description = "create a package archive from a package_manifest.json"
)]
pub struct CreateCommand {
    #[argh(positional, description = "package_manifest.json to archive")]
    pub package_manifest: Utf8PathBuf,

    #[argh(option, description = "output package archive", short = 'o')]
    pub output: Utf8PathBuf,

    #[argh(
        option,
        description = "root directory for paths in package_manifest.json",
        default = "Utf8PathBuf::from(\".\")",
        short = 'r'
    )]
    pub root_dir: Utf8PathBuf,
}
