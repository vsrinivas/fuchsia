// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "restart",
    description = "Restart all Driver Hosts containing the driver specified by driver path. ZX_ERR_NOT_FOUND indicates that there is no driver matching the given path",
    example = "To restart a driver:

    $ ffx driver restart '/boot/driver/e1000.so'",
    error_code(1, "Failed to connect to the driver manager service")
)]
pub struct DriverRestartCommand {
    #[argh(positional, description = "path of the driver to be restarted.")]
    pub driver_path: String,
}
