// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_config::ConfigLevel, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "default",
    description = "Manage the default repository",
    example = "For one-off overrides for the default use `--repository` option:

    $ ffx repository <subcommand> --repository <repository name> ...

Or use the `--config` option:

    $ ffx --config repository.default=<repository name> repository <subcommand>",
    note = "Manages the default configured repository for all operations. The default
repository is designated by a `*` next to the name. This is an alias for the
`repository.default` configuration key."
)]

pub struct RepositoryDefaultCommand {
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum SubCommand {
    Get(RepositoryDefaultGetCommand),
    Set(RepositoryDefaultSetCommand),
    Unset(RepositoryDefaultUnsetCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "unset",
    description = "Clears the default configured repository",
    example = "To clear the default repository:

    $ ffx repository default unset

To clear the `repository.default` key from global configuration:

    $ ffx repository default unset -l global

To specify a specific build directory:

    $ ffx repository default unset -l build -b ~/fuchsia/out",
    note = "Clears the `repository.default` configuration key. By default clears the
'User Configuration'. Returns a warning if the key is already empty."
)]

pub struct RepositoryDefaultUnsetCommand {
    #[argh(option, default = "ConfigLevel::User", short = 'l')]
    /// config level, such as 'user', 'build', or 'global'
    pub level: ConfigLevel,

    #[argh(option, short = 'b')]
    /// optional directory to associate the provided build config
    pub build_dir: Option<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "get",
    description = "Get the default configured repository",
    note = "Returns the default configured repository from the 'User Configuration'.
Returns an empty string if no default is configured."
)]
pub struct RepositoryDefaultGetCommand {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "set",
    description = "Set the default repository",
    example = "To set the default repository:

   $ ffx repository default set <repository name>

To set the 'repository.default` key at the global configuration:

   $ ffx repository default set -l global <repository name>

To specify a default repository for a specific build directory:

   $ ffx repository default set -l build -b ~/fuchsia/out <repository name>",
    note = "Sets the `repository.default` configuration key. By default sets the key in
the 'User Configuration'. Can be used in conjuction with `ffx repository list`
to list the names of the discovered repositorys.

After setting the default repository, `ffx repository list` will mark the default
with a `*` in the output list."
)]

pub struct RepositoryDefaultSetCommand {
    #[argh(positional)]
    /// name of the repository
    pub name: String,

    #[argh(option, default = "ConfigLevel::User", short = 'l')]
    /// config level, such as 'user', 'build', or 'global'
    pub level: ConfigLevel,

    #[argh(option, short = 'b')]
    /// optional directory to associate the provided build config
    pub build_dir: Option<String>,
}
