// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_profile_network_sub_command::SubCommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "network", description = "Access network activity information")]
/// Top-level command for "ffx profile network".
pub struct NetworkCommand {
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}
