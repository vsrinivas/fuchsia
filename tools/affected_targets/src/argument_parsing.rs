// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs)]
/// affected_targets uses build configuration and a list of files to
/// determine whether or not the builder is affected by the changed files.
pub struct ProgramArguments {
    /// the path to the gn tool
    #[argh(option, short = 'g')]
    pub gn_path: String,

    /// the path to the build directory
    #[argh(option, short = 'b')]
    pub build_directory: String,

    /// the path to the source tree
    #[argh(option, short = 's')]
    pub source_directory: String,

    /// if set to true, c++ changes will not be analyzed (i.e., will always return `Build`)
    #[argh(switch)]
    pub disable_cpp: bool,

    /// the files that have been changed
    #[argh(positional, short = 'c')]
    pub changed_files: Vec<String>,
}

impl ProgramArguments {
    pub fn parse() -> ProgramArguments {
        argh::from_env()
    }
}
