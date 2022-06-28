// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_inspect_sub_command::SubCommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "inspect",
    description = "Query component nodes exposed via the Inspect API"
)]
pub struct InspectCommand {
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}
