// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs)]
/// affected_builders uses build configuration and a list of files to
/// determine whether or not the builder is affected by the changed files.
pub struct Arguments {
    #[argh(positional, short = 'b')]
    /// the path to the build directory
    pub build_directory: String,

    /// the files that have been changed
    #[argh(positional, short = 'c')]
    pub changed_files: Vec<String>,
}

impl Arguments {
    pub fn parse() -> Arguments {
        argh::from_env()
    }
}
