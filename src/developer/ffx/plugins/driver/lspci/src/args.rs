// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {argh::FromArgs, ffx_core::ffx_command, lspci::filter::Filter};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "lspci",
    description = "Prints out pci device info",
    example = "To show the device tree:

    $ ffx driver lspci",
    error_code(1, "Failed to connect to the device manager service")
)]
pub struct DriverLspci {
    // Show PCI Info
    #[argh(positional, default = "String::from(\"sys/platform/PCI0/bus\")")]
    /// path to the fuchsia.hardware.pci service
    pub service: String,
    #[argh(switch, short = 'v')]
    /// print verbose device configuration
    pub verbose: bool,
    #[argh(switch, short = 'q')]
    /// don't print errors found trying to parse the database
    pub quiet: bool,
    #[argh(switch, short = 'x')]
    /// dump raw configuration space
    pub print_config: bool,
    #[argh(switch, short = 'n')]
    /// print numeric IDs.
    pub print_numeric: bool,
    #[argh(switch, short = 'N')]
    /// only print numeric IDs.
    pub only_print_numeric: bool,
    #[argh(option, short = 's')]
    /// [[<bus>]:][slot][.[<func>]]    Show only devices in selected slots
    pub filter: Option<Filter>,
}
