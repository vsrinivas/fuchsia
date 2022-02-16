// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_emulator_sub_command::Subcommand};

/// entry point for ffx
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "emu",
    description = "Start and manage Fuchsia emulators.",
    note = "The emu command is used to start up, manage, and shut down Fuchsia emulators.

The `start` subcommand launches an emulator according to the configuration in
the Product Bundle. Once one or more emulators are running, you can use the
`list` subcommand to see the name and status of all running emulators, and the
`show` subcommand to get a printout of the configuration for a specific
emulator. When you're done with an emulator, use the `stop` subcommand to
cleanly terminate that emulator.

For more information on the Fuchsia emulator, see the Getting Started page at
https://fuchsia.dev/fuchsia-src/get-started/set_up_femu."
)]
pub struct EmulatorCommand {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
