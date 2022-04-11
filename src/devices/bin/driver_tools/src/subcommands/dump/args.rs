// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "dump",
    description = "Dump device tree",
    example = "To dump the device tree:

    $ driver dump",
    error_code(1, "Failed to connect to the driver development service"),
    example = "To graph device tree:

    $ driver dump --graph | dot -Tpng | display"
)]
pub struct DumpCommand {
    /// list all device properties.
    #[argh(switch, short = 'v', long = "verbose")]
    pub verbose: bool,

    /// output device graph in dot language so that it may be viewed
    #[argh(switch, short = 'g', long = "graph")]
    pub graph: bool,

    /// if this exists, the user will be prompted for a component to select.
    #[argh(switch, short = 's', long = "select")]
    pub select: bool,
}
