// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list-devices",
    description = "List devices",
    example = "To list all devices:

    $ ffx driver list-devices -v",
    error_code(1, "Failed to connect to the driver development service")
)]
pub struct DriverListDevicesCommand {
    /// specific device to list information about.
    #[argh(positional)]
    pub device: Option<String>,

    /// if this exists, the user will be prompted for a component to select.
    #[argh(switch, short = 's', long = "select")]
    pub select: bool,

    /// list all device properties.
    #[argh(switch, short = 'v', long = "verbose")]
    pub verbose: bool,
}
