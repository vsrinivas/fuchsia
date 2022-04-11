// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "restart",
    description = "Restart all driver hosts containing the driver specified by driver_path.",
    example = "To restart a driver:

    $ driver restart fuchsia-boot:///#driver/e1000.so",
    error_code(1, "Failed to connect to the driver manager service")
)]
pub struct RestartCommand {
    #[argh(positional, description = "path of the driver to be restarted.")]
    pub driver_path: String,

    /// if this exists, the user will be prompted for a component to select.
    #[argh(switch, short = 's', long = "select")]
    pub select: bool,
}
