// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {argh::FromArgs, lspci::filter::Filter};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "lspci",
    description = "Prints out pci device info",
    example = "To show the device tree:

    $ driver lspci",
    error_code(1, "Failed to connect to the device manager service")
)]
pub struct LspciCommand {
    // Show PCI Info
    /// path to the fuchsia.hardware.pci service
    #[argh(positional, default = "String::from(\"sys/platform/platform-passthrough/PCI0/bus\")")]
    pub service: String,

    /// print verbose device configuration
    #[argh(switch, short = 'v')]
    pub verbose: bool,

    /// don't print errors found trying to parse the database
    #[argh(switch, short = 'q')]
    pub quiet: bool,

    /// dump raw configuration space
    #[argh(switch, short = 'x')]
    pub print_config: bool,

    /// print numeric IDs.
    #[argh(switch, short = 'n')]
    pub print_numeric: bool,

    /// only print numeric IDs.
    #[argh(switch, short = 'N')]
    pub only_print_numeric: bool,

    /// [[<bus>]:][slot][.[<func>]]    Show only devices in selected slots
    #[argh(option, short = 's')]
    pub filter: Option<Filter>,

    /// if this exists, the user will be prompted for a component to select.
    #[argh(switch, long = "select")]
    pub select: bool,
}
