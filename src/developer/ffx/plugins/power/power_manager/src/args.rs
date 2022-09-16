// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "debugcmd", description = "Send a debug command to the Power Manager")]
pub struct PowerManagerDebugCommand {
    #[argh(option, description = "name of target node")]
    pub node_name: String,

    #[argh(option, description = "debug command to send")]
    pub command: String,

    #[argh(option, description = "arguments for the debug command")]
    pub args: Vec<String>,
}
