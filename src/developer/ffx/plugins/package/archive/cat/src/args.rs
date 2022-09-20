// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

#[ffx_command()]
#[derive(Eq, FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "cat",
    description = "write the contents of <far_path> inside the Fuchia package archive file to stdout"
)]
pub struct CatCommand {
    #[argh(positional, description = "package archive")]
    pub archive: PathBuf,
    #[argh(positional, description = "path of the file within the archive to write")]
    pub far_path: PathBuf,
    #[argh(switch, description = "treat filename as a blob hash", long = "as-hash")]
    pub as_hash: bool,
}
