// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_config::ConfigLevel;
use ffx_core::ffx_command;
use std::path::PathBuf;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "default",
    description = "Manage the default target",
    example = "For one-off overrides for the default use `--target` option:

    $ ffx --target <target name> <subcommand>

Or use the `--config` option:

    $ ffx --config target.default=<target name> <subcommand>",
    note = "Manages the default configured target for all operations. The default
target is designated by a `*` next to the name. This is an alias for the
`target.default` configuration key."
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
    Unset(TargetDefaultUnsetCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "unset",
    description = "Clears the default configured target",
    example = "To clear the default target:

    $ ffx target default unset

To clear the `target.default` key from global configuration:

    $ ffx target default unset -l global

To specify a specific build directory:

    $ ffx target default unset -l build -b ~/fuchsia/out",
    note = "Clears the `target.default` configuration key. By default clears the
'User Configuration'. Returns a warning if the key is already empty."
)]

pub struct TargetDefaultUnsetCommand {
    #[argh(option, default = "ConfigLevel::User", short = 'l')]
    /// config level, such as 'user', 'build', or 'global'
    pub level: ConfigLevel,

    #[argh(option, short = 'b')]
    /// optional directory to associate the provided build config
    pub build_dir: Option<PathBuf>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "get",
    description = "Get the default configured target",
    note = "Returns the default configured target from the 'User Configuration'.
Returns an empty string if no default is configured."
)]
pub struct TargetDefaultGetCommand {
    #[argh(option, short = 'l')]
    /// config level, such as 'user', 'build', or 'global' - defaults to searching all levels
    pub level: Option<ConfigLevel>,

    #[argh(option, short = 'b')]
    /// optional directory to associate the provided build config
    pub build_dir: Option<PathBuf>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "set",
    description = "Set the default target",
    example = "To set the default target:

   $ ffx target default set <target name>

To set the 'target.default` key at the global configuration:

   $ ffx target default set -l global <target name>

To specify a default target for a specific build directory:

   $ ffx target default set -l build -b ~/fuchsia/out <target name>",
    note = "Sets the `target.default` configuration key. By default sets the key in
the 'User Configuration'. Can be used in conjuction with `ffx target list`
to list the names of the discovered targets.

After setting the default target, `ffx target list` will mark the default
with a `*` in the output list."
)]

pub struct TargetDefaultSetCommand {
    #[argh(positional)]
    /// node name of the target
    pub nodename: String,

    #[argh(option, default = "ConfigLevel::User", short = 'l')]
    /// config level, such as 'user', 'build', or 'global'
    pub level: ConfigLevel,

    #[argh(option, short = 'b')]
    /// optional directory to associate the provided build config
    pub build_dir: Option<PathBuf>,
}
