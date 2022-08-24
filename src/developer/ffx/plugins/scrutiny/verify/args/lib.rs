// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod bootfs;
pub mod component_resolvers;
pub mod kernel_cmdline;
pub mod route_sources;
pub mod routes;
pub mod static_pkgs;
pub mod structured_config;

use {argh::FromArgs, ffx_core::ffx_command, std::path::PathBuf};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "verify", description = "Verify the build")]
pub struct Command {
    /// path to depfile that gathers dependencies during execution.
    #[argh(option)]
    pub depfile: Option<PathBuf>,
    /// path to stamp file to write to if and only if verification succeeds.
    #[argh(option)]
    pub stamp: Option<PathBuf>,
    /// path to directory to use for temporary files.
    #[argh(option)]
    pub tmp_dir: Option<PathBuf>,
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum SubCommand {
    Bootfs(bootfs::Command),
    ComponentResolvers(component_resolvers::Command),
    KernelCmdline(kernel_cmdline::Command),
    RouteSources(route_sources::Command),
    Routes(routes::Command),
    StaticPkgs(static_pkgs::Command),
    StructuredConfig(structured_config::Command),
}
