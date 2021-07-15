// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "download", description = "download Package from TUF package server.")]
pub struct DownloadCommand {
    #[argh(positional, description = "URL of TUF repository")]
    pub tuf_url: String,

    #[argh(positional, description = "URL of Blobs Server")]
    pub blob_url: String,

    #[argh(positional, description = "target_path")]
    pub target_path: String,

    #[argh(
        option,
        short = 'o',
        default = "PathBuf::from(\".\")",
        description = "directory to save package"
    )]
    pub output_path: PathBuf,
}
