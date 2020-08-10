// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "package", description = "Create and publish Fuchsia packages")]
pub struct PackageCommand {
    #[argh(subcommand)]
    pub sub: SubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubCommand {
    Build(BuildCommand),
    Export(ExportCommand),
    Import(ImportCommand),
}

#[derive(FromArgs, PartialEq, Debug, Default)]
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

    #[argh(option, description = "write the package hash to this file instead of stdout")]
    pub hash_out: Option<String>,

    #[argh(option, description = "write a gcc-format depfile for use in build systems")]
    pub depfile: Option<String>,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "export", description = "export a package archive")]
pub struct ExportCommand {
    #[argh(positional, description = "package to export")]
    pub package: String,
    #[argh(option, description = "output", short = 'o')]
    pub output: String,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "import", description = "import a package archive")]
pub struct ImportCommand {
    #[argh(positional, description = "archive to import")]
    pub archive: String,
}
