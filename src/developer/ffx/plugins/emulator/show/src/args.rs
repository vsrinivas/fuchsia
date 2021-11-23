// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;

#[ffx_command()]
#[derive(FromArgs, Default, Debug, PartialEq)]
#[argh(subcommand, name = "show")]
/// Show Fuchsia emulator details.
pub struct ShowCommand {
    /// name of the instance to show, as specified to the start command.
    /// See a list of available instances by running `ffx emu list`.
    #[argh(positional, default = "\"fuchsia-emulator\".to_string()")]
    pub name: String,
}
