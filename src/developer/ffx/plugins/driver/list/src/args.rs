// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list",
    description = "List drivers",
    example = "To list all drivers with properties:

    $ ffx driver list -v",
    error_code(1, "Failed to connect to the driver development service")
)]
pub struct DriverListCommand {
    /// list all driver properties.
    #[argh(switch, short = 'v', long = "verbose")]
    pub verbose: bool,
}
