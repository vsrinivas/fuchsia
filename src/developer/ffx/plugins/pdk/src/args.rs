// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use ffx_pdk_sub_command::Subcommand;

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "pdk", description = "PDK related tool.")]
pub struct PdkCommand {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
