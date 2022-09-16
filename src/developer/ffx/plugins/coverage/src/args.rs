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

    /// path to symbol index json to load symbol index from
    #[argh(option)]
    pub symbol_index_json: Option<PathBuf>,

    /// directory to export HTML reports to
    #[argh(option)]
    pub export_html: Option<PathBuf>,

    /// path to export LCOV file to
    #[argh(option)]
    pub export_lcov: Option<PathBuf>,

    /// "<from>,<to>" remapping of source file paths passed through to llvm-cov
    #[argh(option)]
    pub path_remappings: Vec<String>,

    /// path to the directory used as a base for relative coverage mapping paths, passed through to llvm-cov
    #[argh(option)]
    pub compilation_dir: Option<PathBuf>,

    /// paths to source files to show coverage for
    #[argh(positional)]
    pub src_files: Vec<PathBuf>,
}
