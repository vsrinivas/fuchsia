// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use std::path::PathBuf;

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug, Default)]
#[argh(subcommand, name = "update", description = "Update the artifact_lock.json")]
pub struct UpdateCommand {
    #[argh(option, description = "path to the artifact_spec.json file.")]
    pub spec_file: PathBuf,

    #[argh(
        option,
        short = 'o',
        default = "PathBuf::from(\"./artifact_lock.json\")",
        description = "path to the output artifact_lock.json file."
    )]
    pub out: PathBuf,

    #[argh(
        option,
        description = "path to root directory for local artifact_groups.json file. This parameter is required only when a local artifact store is used."
    )]
    pub artifact_root: Option<String>,
}
