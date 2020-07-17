// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "package", description = "Packaging tools")]
pub struct PackageCommand {
    #[argh(subcommand)]
    pub sub: SubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubCommand {
    Build(BuildCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "build",
    description = "Builds a package.

Entries may be specified as:
- <dst>=<src>: Place the file at path <src> into the package at path <dst>.
- @<manifest-file>: Read each line of this file as an entry. This is not recursive; you can't put another @<manifest-file> 
"
)]
pub struct BuildCommand {
    #[argh(positional, description = "package entries")]
    pub entries: Vec<String>,
    #[argh(
        option,
        description = "base directory for the <src> part of entries; defaults to the current directory"
    )]
    pub source_dir: Option<String>,
}
