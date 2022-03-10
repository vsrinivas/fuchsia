// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::str::FromStr};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "knock",
    description = "Connects to a capability described by a given selector",
    example = "To connect to a service:

    $ ffx component knock /core/appmgr fuchsia.hwinfo.Product --node out",
    note = "Knock verifies the existence of a capability by attempting to connect to it.

Note that wildcards can be used for the service parameter but must match exactly one service.

`ffx component select` can be used to explore the component
topology and find the correct selector for use in this command."
)]
pub struct KnockCommand {
    #[argh(positional)]
    /// the component's moniker. Example: `/core/appmgr`.
    pub moniker: String,

    #[argh(positional)]
    /// the service to knock. Example: `fuchsia.hwinfo.Product`.
    /// Can be a wildcard `*` if it matches exactly one service.
    pub service: String,

    #[argh(option, default = "Node::Out")]
    /// the namespace, also called node selector. Correspond to the routing terminology used in the component manifest.
    /// One of ['in', 'out', 'expose']. Defaults to 'out'
    pub node: Node,

    #[argh(option, default = "5")]
    /// the time in seconds to wait when status is SHOULD_WAIT before assuming success.
    /// Defaults to 5 second.
    pub timeout: u64,
}

#[derive(PartialEq, Debug)]
pub enum Node {
    In,
    Out,
    Expose,
}

impl FromStr for Node {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "in" => Ok(Node::In),
            "out" => Ok(Node::Out),
            "expose" => Ok(Node::Expose),
            _ => Err(format!(
                "'{}' is not a valid node selector: Must be one of 'in', 'out' or 'expose'",
                s
            )),
        }
    }
}
impl ToString for Node {
    fn to_string(&self) -> String {
        match self {
            Node::In => String::from("in"),
            Node::Out => String::from("out"),
            Node::Expose => String::from("expose"),
        }
    }
}
