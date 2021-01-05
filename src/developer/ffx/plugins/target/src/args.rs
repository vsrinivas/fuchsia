// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_target_sub_command::Subcommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "target",
    description = "Interact with a target device or emulator",
    note = "The `target` subcommand contains various commands for target management
and interaction.

Typically, this is the entry workflow for users, allowing for target
discovery and provisioning before moving on to `component` or `session`
workflows once the system is up and running on the target.

Most of the commands depend on the RCS (Remote Control Service) on the
target."
)]
pub struct TargetCommand {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
