// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/104019): Consider enabling globally.
#![deny(unused_crate_dependencies)]
#![warn(clippy::all)]

use {
    anyhow::Result,
    argh::FromArgs,
    package_tool::{
        cmd_package_build, cmd_repo_create, cmd_repo_publish, PackageBuildCommand,
        RepoCreateCommand, RepoPublishCommand,
    },
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
    Package(PackageCommand),
    Repository(RepoCommand),
}

/// Package subcommands
#[derive(FromArgs)]
#[argh(subcommand, name = "package")]
struct PackageCommand {
    #[argh(subcommand)]
    subcommands: PackageSubCommands,
}

#[derive(FromArgs)]
#[argh(subcommand)]
enum PackageSubCommands {
    Build(PackageBuildCommand),
}

/// Repository subcommands
#[derive(FromArgs)]
#[argh(subcommand, name = "repository")]
struct RepoCommand {
    #[argh(subcommand)]
    subcommands: RepoSubCommands,
}

#[derive(FromArgs)]
#[argh(subcommand)]
enum RepoSubCommands {
    Create(RepoCreateCommand),
    Publish(RepoPublishCommand),
}

#[fuchsia::main]
async fn main() -> Result<()> {
    let cmd: Command = argh::from_env();
    match cmd.subcommands {
        SubCommands::Package(cmd) => match cmd.subcommands {
            PackageSubCommands::Build(cmd) => cmd_package_build(cmd).await,
        },
        SubCommands::Repository(cmd) => match cmd.subcommands {
            RepoSubCommands::Create(cmd) => cmd_repo_create(cmd).await,
            RepoSubCommands::Publish(cmd) => cmd_repo_publish(cmd).await,
        },
    }
}
