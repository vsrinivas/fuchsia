// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/104019): Consider enabling globally.
#![deny(unused_crate_dependencies)]

use {
    anyhow::Result,
    argh::FromArgs,
    package_tool::{cmd_package_build, BuildCommand},
};

/// Package manipulation tool
#[derive(FromArgs)]
struct Command {
    #[argh(subcommand)]
    subcommands: SubCommands,
}

#[derive(FromArgs)]
#[argh(subcommand)]
enum SubCommands {
    Build(BuildCommand),
}

#[fuchsia::main]
async fn main() -> Result<()> {
    let cmd: Command = argh::from_env();
    match cmd.subcommands {
        SubCommands::Build(cmd) => cmd_package_build(cmd).await,
    }
}
