// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_sl4f_plugin_sub_command::SubCommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "sl4f", description = "Manage the sl4f server")]
pub struct Sl4fCommand {
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}
