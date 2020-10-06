// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_config::ConfigLevel, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "default",
    description = "Get or set the default target device or emulator."
)]
pub struct TargetDefaultCommand {
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum SubCommand {
    Get(TargetDefaultGetCommand),
    Set(TargetDefaultSetCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "get",
    description = "gets the default target (functional alias for `config get target.default`)"
)]
pub struct TargetDefaultGetCommand {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "set",
    description = "sets the default target (functional alias for `config set target.default`)"
)]
pub struct TargetDefaultSetCommand {
    #[argh(positional)]
    /// nodename of the target to set as default.
    pub nodename: String,

    #[argh(option, default = "ConfigLevel::User", short = 'l')]
    /// config level. Possible values: "user", "build", "global". Defaults to "user".
    pub level: ConfigLevel,

    #[argh(option, short = 'b')]
    /// optional build directory to associate the build config provided -
    /// used for "build" configs.
    pub build_dir: Option<String>,
}
