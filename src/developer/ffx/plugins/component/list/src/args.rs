// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, component_hub::list::ListFilter, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list",
    description = "Lists all components in the component topology",
    example = "To list all components in the topology:

    $ ffx component list

    To list all cmx components in the topology:

    $ ffx component list --only cmx

    To list all cml components in the topology:

    $ ffx component list --only cml

    To list all running components in the topology:

    $ ffx component list --only running

    To list all stopped components in the topology:

    $ ffx component list --only stopped"
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
