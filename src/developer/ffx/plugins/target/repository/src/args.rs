// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_target_repository_sub_command::Subcommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "repository",
    description = "Interact with target repository registration",
    note = "The `repository` subcommand contains various commands for repository management
    and interaction on targets.

    Most of the commands depend on the RCS (Remote Control Service) on the target."
)]
pub struct TargetCommand {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
