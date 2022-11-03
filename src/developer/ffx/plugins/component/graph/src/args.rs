// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, component_debug::list::ListFilter, ffx_core::ffx_command, std::str::FromStr};

/// Determines the visual orientation of the graph's nodes.
#[derive(Debug, PartialEq)]
pub enum GraphOrientation {
    /// The graph's nodes should be ordered from top to bottom.
    TopToBottom,
    /// The graph's nodes should be ordered from left to right.
    LeftToRight,
    /// The graph's nodes should be ordered in GraphViz's default orientation.
    Default,
}

impl FromStr for GraphOrientation {
    type Err = &'static str;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().replace("_", "").replace("-", "").as_str() {
            "tb" | "toptobottom" => Ok(GraphOrientation::TopToBottom),
            "lr" | "lefttoright" => Ok(GraphOrientation::LeftToRight),
            _ => Err("graph orientation should be 'top_to_bottom' or 'left_to_right'."),
        }
    }
}

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

    $ ffx component graph --only relatives:foo
    
    To order the graph's nodes from left-to-right (instead of top-to-bottom):
    
    $ ffx component graph --orientation left_to_right"
)]

pub struct ComponentGraphCommand {
    #[argh(option, long = "only", short = 'o')]
    /// outputs only cmx/cml/running/stopped components depending on the flag.
    pub only: Option<ListFilter>,

    #[argh(option, long = "orientation", short = 'r')]
    /// changes the visual orientation of the graph's nodes.
    /// Allowed values are "left_to_right"/"lr" and "top_to_bottom"/"tb".
    pub orientation: Option<GraphOrientation>,
}
