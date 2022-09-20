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
    name = "extract",
    description = "extract  the contents of <far_path> inside the Fuchia package archive file to the output directory"
)]
pub struct ExtractCommand {
    #[argh(
        switch,
        short = 'v',
        description = "verbose output. Print file names as they are extracted"
    )]
    pub verbose: bool,
    #[argh(
        option,
        short = 'o',
        long = "output-dir",
        description = "output directory for writing the extracted files. Defaults to the current directory.",
        default = "PathBuf::from(\".\")"
    )]
    pub output_dir: PathBuf,
    #[argh(positional, description = "package archive")]
    pub archive: PathBuf,
    #[argh(positional, description = "files to extract")]
    pub far_paths: Vec<PathBuf>,
    #[argh(switch, description = "treat filenames as blob hashes", long = "as-hash")]
    pub as_hash: bool,
}
