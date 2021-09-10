// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug, Default)]
#[argh(subcommand, name = "fetch", description = "Fetch the artifacts based on artifact_lock.json")]
pub struct FetchCommand {
    #[argh(option, description = "path to the artifact_lock.json file")]
    pub lock_file: PathBuf,

    #[argh(
        option,
        short = 'o',
        default = "PathBuf::from(\".\")",
        description = "path to the output directory"
    )]
    pub out: PathBuf,

    #[argh(option, short = 'm', description = "merkle of the blob to download")]
    pub merkle: Option<String>,

    #[argh(option, short = 'a', description = "name of the artifact to download")]
    pub artifact: Option<String>,

    #[argh(
        option,
        description = "path to local artifact_store. This parameter is required only when a local artifact store is used."
    )]
    pub local_dir: Option<String>,

    #[argh(switch, description = "fetch progress will be shown if this flag is set.")]
    pub show_progress: bool,
}
