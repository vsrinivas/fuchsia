// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_scrutiny_list_sub_command::Subcommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "list", description = "Lists properties about build artifacts")]
pub struct ScrutinyListCommand {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
