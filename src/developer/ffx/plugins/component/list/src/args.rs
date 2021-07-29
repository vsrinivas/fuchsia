// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, component_hub::list::ListFilter, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list",
    description = "List all components, with the option of listing only cmx/cml components",
    example = "To list all components in the topology:

    $ ffx component list

    To list all cmx components in the topology:

    $ ffx component list --only cmx

    To list all cml components in the topology:

    $ ffx component list --only cml

    To list all running components in the topology:

    $ ffx component list --only running

    To list all stopped components in the topology:

    $ ffx component list --only stopped",
    note = "Lists all the components on the running target. If no <only> is entered,
the default option outputs a tree of all components on the system. If a valid <only>
is entered, the command outputs a tree of only cmx/cml/running/stopped components in the system.

If the command fails or times out, ensure RCS is running on the target.
This can be verified by running `ffx target list` and seeing the status
on the RCS column.",
    error_code(1, "The command has timed out")
)]

pub struct ComponentListCommand {
    #[argh(option, long = "only", short = 'o')]
    /// output only cmx/cml/running/stopped components depending on the flag.
    pub only: Option<ListFilter>,

    #[argh(switch, long = "verbose", short = 'v')]
    /// whether or not to display a column showing component type and a column
    /// showing running/stopped.
    pub verbose: bool,
}
