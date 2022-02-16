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
    SERIAL = The serial number of the target.
    TYPE = The product type of the target.
    STATE = The high-level state of the target.
    ADDRS/IP = The discovered and known addresses of the target.
    RCS = Indicates if the Remote Control Service is running on the target.

The NAME column shows the target's advertised name. When the target is
in early boot state such as fastboot, the NAME column may be `<unknown>` with
a STATE being `fastboot` and a SERIAL attribute.

By default, the `list` command outputs in a tabular format. To override
the format, pass `--format` and can take the following options: 'simple'
, 'tabular|table|tab', 'addresses|addrs|addr', 'name-only', 'json|JSON' or
in short form 's', 't', 'a', 'n', 'j'.

By default, Zedboot discovery is disabled.  To enable discovery of Zedboot
targets run:

    $ ffx config set discovery.zedboot.enabled true
",
    error_code(
        2,
        "If a nodename is supplied, an error code of 2 will be returned \
               if the nodename cannot be resolved"
    )
)]
pub struct ListCommand {
    #[argh(positional)]
    pub nodename: Option<String>,

    #[argh(option, short = 'f', default = "Format::Tabular")]
    /// determines the output format for the list operation
    pub format: Format,
}

#[derive(Debug, PartialEq)]
pub enum Format {
    Tabular,
    Simple,
    Addresses,
    NameOnly,
    Json,
}

impl std::str::FromStr for Format {
    type Err = Error;

    fn from_str(s: &str) -> Result<Format> {
        match s {
            "tabular" | "table" | "tab" | "t" => Ok(Format::Tabular),
            "simple" | "s" => Ok(Format::Simple),
            "addresses" | "a" | "addr" | "addrs" => Ok(Format::Addresses),
            "name-only" | "n" => Ok(Format::NameOnly),
            "json" | "JSON" | "j" => Ok(Format::Json),
            _ => Err(anyhow!("expected 'tabular', 'simple', 'addresses', or 'json'")),
        }
    }
}
