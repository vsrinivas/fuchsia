// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_platform_sub_command::Subcommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "platform", description = "Manage platform build prerequisites")]
pub struct ComponentCommand {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
