// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev:76549): Replace with GN config once available in an ffx_plugin.
#![deny(unused_results)]

#[ffx_core::ffx_command]
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "net", description = "View and manage target network configuration")]
pub struct Command {
    #[argh(subcommand, description = "network configuration command to execute on target device")]
    pub cmd: net_cli::CommandEnum,
}
