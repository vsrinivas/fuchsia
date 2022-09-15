// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "start", description = "run as daemon")]
pub struct StartCommand {
    #[argh(option)]
    /// override the path the socket will be bound to
    pub path: Option<PathBuf>,
}
