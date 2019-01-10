// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::PathBuf;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
pub struct Opt {
    #[structopt(short = "s", long = "stamp", parse(from_os_str))]
    /// Stamp this file on success
    pub stamp: Option<PathBuf>,

    #[structopt(subcommand)]
    pub cmd: Commands,
}

#[derive(StructOpt, Debug)]
pub enum Commands {
    #[structopt(name = "validate")]
    /// validate that one or more cmx files are valid
    Validate {
        #[structopt(name = "FILE", parse(from_os_str))]
        /// files to process
        files: Vec<PathBuf>,
    },

    #[structopt(name = "merge")]
    /// merge the listed cmx files
    Merge {
        #[structopt(name = "FILE", parse(from_os_str))]
        /// files to process
        files: Vec<PathBuf>,

        #[structopt(short = "o", long = "output", parse(from_os_str))]
        /// file to write the merged results to, will print to stdout if not provided
        output: Option<PathBuf>,
    },

    #[structopt(name = "format")]
    /// format a json file
    Format {
        #[structopt(name = "FILE", parse(from_os_str))]
        /// file to format
        file: PathBuf,

        #[structopt(short = "p", long = "pretty")]
        /// whether to pretty-print the results, will minify if not provided
        pretty: bool,

        #[structopt(short = "o", long = "output", parse(from_os_str))]
        /// file to write the formatted results to, will print to stdout if not provided
        output: Option<PathBuf>,
    },

    #[structopt(name = "compile")]
    /// compile a CML file
    Compile {
        #[structopt(name = "FILE", parse(from_os_str))]
        /// file to format
        file: PathBuf,

        #[structopt(short = "p", long = "pretty")]
        /// whether to pretty-print the results, will minify if not provided
        pretty: bool,

        #[structopt(short = "o", long = "output", parse(from_os_str))]
        /// file to write the formatted results to, will print to stdout if not provided
        output: Option<PathBuf>,
    },
}
