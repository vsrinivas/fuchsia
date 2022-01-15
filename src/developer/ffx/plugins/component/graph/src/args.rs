// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, component_hub::list::ListFilter, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "graph",
    description = "Outputs a Graphviz dot graph for the component topology",
    example = "To graph all components in the topology:

    $ ffx component graph

    To graph all cmx components in the topology:

    $ ffx component graph --only cmx

    To graph all cml components in the topology:

    $ ffx component graph --only cml

    To graph all running components in the topology:

    $ ffx component graph --only running

    To graph all stopped components in the topology:

    $ ffx component graph --only stopped

    To graph the ancestors of a component named `foo`:

    $ ffx component graph --only ancestor:foo

    To graph the descendants of a component named `foo`:

    $ ffx component graph --only descendant:foo

    To graph both the ancestors and descendants of a component named `foo`:

    $ ffx component graph --only relatives:foo"
)]

pub struct ComponentGraphCommand {
    #[argh(option, long = "only", short = 'o')]
    /// output only cmx/cml/running/stopped components depending on the flag.
    pub only: Option<ListFilter>,
}
