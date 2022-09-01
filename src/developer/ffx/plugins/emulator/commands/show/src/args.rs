// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;

#[ffx_command()]
#[derive(FromArgs, Default, Debug, PartialEq)]
#[argh(subcommand, name = "show")]
/// Show Fuchsia emulator details.
pub struct ShowCommand {
    /// switch to show the entire config output. This is the default output if
    /// no other switches are invoked.
    #[argh(switch)]
    pub all: bool,

    /// switch to show the command line used to launch the emulator.
    #[argh(switch)]
    pub cmd: bool,

    /// name of the emulator instance to show details for.
    /// See a list of available instances by running `ffx emu list`.
    /// If only one instance is running, this defaults to that instance name.
    #[argh(positional)]
    pub name: Option<String>,

    /// switch to show network details.
    #[argh(switch)]
    pub net: bool,
}
