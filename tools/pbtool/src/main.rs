// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Manage product bundles, prepare them for OTAs, and convert them to build
//! archives.

mod build_archive;

use anyhow::Result;
use argh::FromArgs;
use build_archive::GenerateBuildArchive;

/// Tool for managing product bundles that is specific to fuchsia infrastructure.
#[derive(FromArgs, PartialEq, Debug)]
struct Command {
    /// the nested subcommands.
    #[argh(subcommand)]
    sub: Subcommand,
}

/// Subcommands for pbtool.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum Subcommand {
    /// Generate a build archive.
    GenerateBuildArchive(GenerateBuildArchive),
}

fn main() -> Result<()> {
    let command = argh::from_env::<Command>();
    match command.sub {
        Subcommand::GenerateBuildArchive(cmd) => cmd.generate(),
    }
}
