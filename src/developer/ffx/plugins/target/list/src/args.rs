// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error, Result},
    argh::FromArgs,
    ffx_core::ffx_command,
};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list",
    example = "To list targets in short form:

    $ ffx target list --format s
    fe80::4415:3606:fb52:e2bc%zx-f80ff974f283 pecan-guru-clerk-rhyme

To list targets with only their addresses:

    $ ffx target list --format a
    fe80::4415:3606:fb52:e2bc%zx-f80ff974f283",
    description = "List all targets",
    note = "List all targets that the daemon currently has in memory. This includes
manually added targets. The daemon also proactively discovers targets as
they come online. Use `ffx target list` to always get the latest list
of targets.

The default target is marked with a '*' next to the node name. The table
has the following columns:

    NAME = The name of the target.
    TYPE = The product type of the target, currently always 'Unknown'.
    STATE = The high-level state of the target, currently always 'Unknown'.
    AGE = Shows the last time the daemon was able to discover the target.
    ADDRS/IP = The discovered and known addresses of the target.
    RCS = Indicates if the Remote Control Service is running on the target.

The NAME column shows the target's advertised name. When the target is
in early boot state such as fastboot, shows 'FastbootDevice' with the
`product` and `serial` attributes instead.

By default, the `list` command outputs in a tabular format. To override
the format, pass `--format` and can take the following options: 'simple'
, 'tabular|table|tab', 'addresses|addrs|addr' or in short form 's', 't',
 'a'."
)]
pub struct ListCommand {
    #[argh(positional)]
    pub nodename: Option<String>,

    #[argh(option, default = "Format::Tabular")]
    /// determines the output format for the list operation
    pub format: Format,
}

#[derive(Debug, PartialEq)]
pub enum Format {
    Tabular,
    Simple,
    Addresses,
}

impl std::str::FromStr for Format {
    type Err = Error;

    fn from_str(s: &str) -> Result<Format> {
        match s {
            "tabular" | "table" | "tab" | "t" => Ok(Format::Tabular),
            "simple" | "s" => Ok(Format::Simple),
            "addresses" | "a" | "addr" | "addrs" => Ok(Format::Addresses),
            _ => Err(anyhow!("expected 'tabular', 'simple', or 'addresses'")),
        }
    }
}
