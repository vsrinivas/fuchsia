// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::path::PathBuf};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "coverage", description = "Show coverage from test outputs")]

pub struct CoverageCommand {
    /// path to ffx test output directory
    #[argh(option)]
    pub test_output_dir: PathBuf,

    /// path to clang directory, llvm-profdata and llvm-cov are expected in clang_dir/bin
    #[argh(option)]
    pub clang_dir: PathBuf,

    /// paths to source files to show coverage for
    #[argh(positional)]
    pub src_files: Vec<PathBuf>,

    /// paths to instrumented binary files
    // TODO(https://fxbug.dev/99951): Remove this arg when we can look up binary files with
    // symbolizer.
    #[argh(option)]
    pub bin_file: Vec<PathBuf>,
}
