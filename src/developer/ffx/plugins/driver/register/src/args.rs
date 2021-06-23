// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "register",
    description = "Informs the driver manager that a new driver package is available. The driver manager will cache a copy of the driver",
    example = "To register a driver

    $ ffx driver register 'fuchsia-pkg://fuchsia.com/example_driver#meta/example_driver.cmx'",
    error_code(1, "Failed to connect to the driver registrar service")
)]
pub struct DriverRegisterCommand {
    #[argh(positional, description = "component URL of the driver to be registered.")]
    pub url: String,
}
