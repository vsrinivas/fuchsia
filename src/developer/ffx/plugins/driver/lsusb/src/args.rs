// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "lsusb",
    description = "Print the device tree of the target to stdout",
    example = "To show the device tree:

    $ ffx driver lsusb",
    error_code(1, "Failed to connect to the device manager service")
)]
pub struct DriverLsusbCommand {
    #[argh(switch, short = 't')] // TODO
    /// prints USB device tree
    pub tree: bool,
    #[argh(switch, short = 'v')]
    /// verbose output (prints descriptors)
    pub verbose: bool,
    #[argh(option, short = 'c')]
    /// prints configuration descriptor for specified configuration (rather than
    /// current configuration)
    pub configuration: Option<u8>,
    #[argh(switch, short = 'd')] // TODO
    /// prints only specified device
    pub debug: bool,
}
