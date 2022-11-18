// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list-node-groups",
    description = "List Node Groups.",
    example = "To list all Node Groups with properties:

    $ driver list-node-groups -v

To show a specific Node Group, specify a `--name` or `-n` for short:

    $ driver list-node-groups -n example_group",
    error_code(1, "Failed to connect to the driver development service")
)]
pub struct ListNodeGroupsCommand {
    /// list all driver properties.
    #[argh(switch, short = 'v', long = "verbose")]
    pub verbose: bool,

    /// only show the Node Group with this name.
    #[argh(option, short = 'n', long = "name")]
    pub name: Option<String>,

    /// if this exists, the user will be prompted for a component to select.
    #[argh(switch, short = 's', long = "select")]
    pub select: bool,
}
