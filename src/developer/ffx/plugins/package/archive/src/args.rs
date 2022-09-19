// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use ffx_package_archive_sub_command::SubCommand;

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "archive", description = "Archive Fuchsia packages")]
pub struct ArchiveCommand {
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}
