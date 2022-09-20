// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

#[derive(Eq, FromArgs, PartialEq, Debug)]
#[ffx_command()]
#[argh(subcommand, name = "list", description = "List the contents of Fuchia package archive file")]
pub struct ListCommand {
    #[argh(positional, description = "package archive")]
    pub archive: PathBuf,
    #[argh(switch, short = 'l', description = "show long information for each entry")]
    pub long_format: bool,
}
