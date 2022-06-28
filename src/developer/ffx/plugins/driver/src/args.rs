// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, driver_tools::args::DriverSubCommand, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "driver", description = "Support driver development workflows")]
pub struct DriverCommand {
    #[argh(subcommand)]
    pub subcommand: DriverSubCommand,
}

impl Into<driver_tools::args::DriverCommand> for DriverCommand {
    fn into(self) -> driver_tools::args::DriverCommand {
        driver_tools::args::DriverCommand { subcommand: self.subcommand }
    }
}
