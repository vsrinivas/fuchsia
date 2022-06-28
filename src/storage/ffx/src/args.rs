// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[ffx_core::ffx_command()]
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "storage", description = "Manage Fuchsia Filesystems")]
pub struct StorageCommand {
    #[argh(subcommand)]
    pub subcommand: ffx_storage_sub_command::SubCommand,
}
