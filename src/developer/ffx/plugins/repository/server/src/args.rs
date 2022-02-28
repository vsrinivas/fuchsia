// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_repository_server_sub_command::Subcommand};

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "server", description = "Inspect and manage the repository server")]
pub struct ServerCommand {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
