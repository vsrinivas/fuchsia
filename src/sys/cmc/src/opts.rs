// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::{Path, PathBuf};
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

        #[structopt(long = "extra_schema", parse(from_str = "parse_extra_schema_arg"))]
        /// extra JSON schema files to additionally validate against. A custom error message - to
        /// be displayed if the schema fails to validate - can be specified by adding a ':'
        /// separator and the message after the path.
        extra_schemas: Vec<(PathBuf, Option<String>)>,
    },

    #[structopt(name = "merge")]
    /// merge the listed cmx files
    Merge {
        #[structopt(name = "FILE", parse(from_os_str))]
        /// files to process
        ///
        /// If any file contains an array at its root, every object in the array
        /// will be merged into the final object.
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
        /// whether to pretty-print the results (otherwise minify JSON documents; ignored for JSON5)
        pretty: bool,

        #[structopt(long = "cml")]
        /// interpret input file as JSON5 CML, and output in the preferred style, preserving all
        /// comments (this is the default for `.cml` files; implies `--pretty`)
        cml: bool,

        #[structopt(short = "i", long = "in-place")]
        /// replace the input file with the formatted output (implies `--output <inputfile>`)
        inplace: bool,

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

fn parse_extra_schema_arg(src: &str) -> (PathBuf, Option<String>) {
    let v: Vec<&str> = src.splitn(2, ':').collect();
    (Path::new(v[0]).to_path_buf(), v.get(1).map(|s| s.to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! test_parse_extra_schema_arg {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    assert_eq!(parse_extra_schema_arg($input), $result)
                }
            )+
        }
    }

    test_parse_extra_schema_arg! {
        test_parse_extra_schema_arg_schema_only => {
            input = "/some/path",
            result = (Path::new("/some/path").to_path_buf(), None),
        },
        test_parse_extra_schema_arg_schema_and_msg => {
            input = "/some/path:my error message",
            result = (Path::new("/some/path").to_path_buf(), Some("my error message".to_string())),
        },
        test_parse_extra_schema_arg_msg_with_sep => {
            input = "/some/path:my:error:message",
            result = (Path::new("/some/path").to_path_buf(), Some("my:error:message".to_string())),
        },
    }
}
