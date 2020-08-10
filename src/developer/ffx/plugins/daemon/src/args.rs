// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_daemon_plugin_sub_command::Subcommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "daemon", description = "Interact with/control the ffx daemon")]
pub struct DaemonCommand {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
