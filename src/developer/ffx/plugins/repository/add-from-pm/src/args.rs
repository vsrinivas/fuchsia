// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::path::PathBuf};

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "add-from-pm", description = "")]
pub struct AddFromPmCommand {
    /// repositories will be named `NAME`. Defaults to `devhost`.
    #[argh(positional)]
    pub name: String,

    /// path to the pm-built package repository.
    #[argh(positional)]
    pub pm_repo_path: PathBuf,
}
