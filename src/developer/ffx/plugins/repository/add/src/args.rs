// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::path::PathBuf};

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "add", description = "")]
pub struct AddCommand {
    /// repositories will be named `NAME`. Defaults to `devhost`.
    #[argh(option, default = "\"devhost\".to_string()")]
    pub name: String,

    /// path to the package repository.
    #[argh(positional)]
    pub repo_path: PathBuf,
}
