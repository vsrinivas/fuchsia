// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[ffx_core::ffx_command()]
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "blackout", description = "Power failure testing for the filesystems")]
pub struct BlackoutCommand {
    #[argh(subcommand)]
    pub subcommand: ffx_storage_blackout_sub_command::SubCommand,
}
