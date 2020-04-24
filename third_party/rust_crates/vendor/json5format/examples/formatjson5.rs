// Copyright (c) 2020 Google LLC All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//! A command line interface (CLI) tool to format [JSON5](https://json5.org) ("JSON for
//! Humans") documents to a consistent style, preserving comments.
//!
//! See [json5format](../json5format/index.html) for more details.
//!
//! # Usage
//!
//!     formatjson5 [FLAGS] [OPTIONS] [files]...
//!
//!     FLAGS:
//!     -h, --help                  Prints help information
//!     -n, --no_trailing_commas    Suppress trailing commas (otherwise added by default)
//!     -o, --one_element_lines     Objects or arrays with a single child should collapse to a
//!                                 single line; no trailing comma
//!     -r, --replace               Replace (overwrite) the input file with the formatted result
//!     -s, --sort_arrays           Sort arrays of primitive values (string, number, boolean, or
//!                                 null) lexicographically
//!     -V, --version               Prints version information
//!
//!     OPTIONS:
//!     -i, --indent <indent>    Indent by the given number of spaces [default: 4]
//!
//!     ARGS:
//!     <files>...    Files to format (use "-" for stdin)

#![warn(missing_docs)]

use anyhow;
use anyhow::Result;
use json5format::*;
use std::fs;
use std::io;
use std::io::{Read, Write};
use std::path::PathBuf;
use structopt::StructOpt;

/// Parses each file in the given `files` vector and returns a parsed object for each JSON5
/// document. If the parser encounters an error in any input file, the command aborts without
/// formatting any of the documents.
fn parse_documents(files: Vec<PathBuf>) -> Result<Vec<ParsedDocument>, anyhow::Error> {
    let mut parsed_documents = Vec::with_capacity(files.len());
    for file in files {
        let filename = file.clone().into_os_string().to_string_lossy().to_string();
        let mut buffer = String::new();
        if filename == "-" {
            Opt::from_stdin(&mut buffer)?;
        } else {
            fs::File::open(&file)?.read_to_string(&mut buffer)?;
        }

        parsed_documents.push(ParsedDocument::from_string(buffer, Some(filename))?);
    }
    Ok(parsed_documents)
}

/// Formats the given parsed documents, applying the given format `options`. If `replace` is true,
/// each input file is overwritten by its formatted version.
fn format_documents(
    parsed_documents: Vec<ParsedDocument>,
    options: FormatOptions,
    replace: bool,
) -> Result<(), anyhow::Error> {
    let format = Json5Format::with_options(options)?;
    for (index, parsed_document) in parsed_documents.iter().enumerate() {
        let filename = parsed_document.filename().as_ref().unwrap();
        let bytes = format.to_utf8(&parsed_document)?;
        if replace {
            Opt::write_to_file(filename, &bytes)?;
        } else {
            if index > 0 {
                println!();
            }
            if parsed_documents.len() > 1 {
                println!("{}:", filename);
                println!("{}", "=".repeat(filename.len()));
            }
            print!("{}", std::str::from_utf8(&bytes)?);
        }
    }
    Ok(())
}

/// The entry point for the [formatjson5](index.html) command line interface.
fn main() -> Result<()> {
    let args = Opt::args();

    if args.files.len() == 0 {
        return Err(anyhow::anyhow!("No files to format"));
    }

    let parsed_documents = parse_documents(args.files)?;

    let options = FormatOptions {
        indent_by: args.indent,
        trailing_commas: !args.no_trailing_commas,
        collapse_containers_of_one: args.one_element_lines,
        sort_array_items: args.sort_arrays,
        ..Default::default()
    };

    format_documents(parsed_documents, options, args.replace)
}

/// Command line options defined via the structopt! macrorule. These definitions generate the
/// option parsing, validation, and [usage documentation](index.html).
#[derive(Debug, StructOpt)]
#[structopt(
    name = "json5format",
    about = "Format JSON5 documents to a consistent style, preserving comments."
)]
struct Opt {
    /// Files to format (use "-" for stdin)
    #[structopt(parse(from_os_str))]
    files: Vec<PathBuf>,

    /// Replace (overwrite) the input file with the formatted result
    #[structopt(short, long)]
    replace: bool,

    /// Suppress trailing commas (otherwise added by default)
    #[structopt(short, long)]
    no_trailing_commas: bool,

    /// Objects or arrays with a single child should collapse to a single line; no trailing comma
    #[structopt(short, long)]
    one_element_lines: bool,

    /// Sort arrays of primitive values (string, number, boolean, or null) lexicographically
    #[structopt(short, long)]
    sort_arrays: bool,

    /// Indent by the given number of spaces
    #[structopt(short, long, default_value = "4")]
    indent: usize,
}

#[cfg(not(test))]
impl Opt {
    fn args() -> Self {
        Self::from_args()
    }

    fn from_stdin(mut buf: &mut String) -> Result<usize, io::Error> {
        io::stdin().read_to_string(&mut buf)
    }

    fn write_to_file(filename: &str, bytes: &[u8]) -> Result<(), io::Error> {
        fs::OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(filename)?
            .write_all(&bytes)
    }
}

#[cfg(test)]
impl Opt {
    fn args() -> Self {
        if let Some(test_args) = unsafe { &self::tests::TEST_ARGS } {
            Self::from_clap(
                &Self::clap()
                    .get_matches_from_safe(test_args)
                    .expect("failed to parse TEST_ARGS command line arguments"),
            )
        } else {
            Self::from_args()
        }
    }

    fn from_stdin(mut buf: &mut String) -> Result<usize, io::Error> {
        if let Some(test_buffer) = unsafe { &mut self::tests::TEST_BUFFER } {
            *buf = test_buffer.clone();
            Ok(buf.as_bytes().len())
        } else {
            io::stdin().read_to_string(&mut buf)
        }
    }

    fn write_to_file(filename: &str, bytes: &[u8]) -> Result<(), io::Error> {
        if filename == "-" {
            let buf = std::str::from_utf8(&bytes)
                .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;
            if let Some(test_buffer) = unsafe { &mut self::tests::TEST_BUFFER } {
                *test_buffer = buf.to_string();
            } else {
                print!("{}", buf);
            }
            Ok(())
        } else {
            fs::OpenOptions::new()
                .create(true)
                .truncate(true)
                .write(true)
                .open(filename)?
                .write_all(&bytes)
        }
    }
}

#[cfg(test)]
mod tests {

    use super::*;

    pub(crate) static mut TEST_ARGS: Option<Vec<&str>> = None;
    pub(crate) static mut TEST_BUFFER: Option<String> = None;

    #[test]
    fn test_main() {
        let example_json5 = r##"{
    offer: [
        {
            runner: "elf",
        },
        {
            from: "framework",
            to: "#elements",
            protocol: "/svc/fuchsia.sys2.Realm",
        },
        {
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
            from: "realm",
        },
    ],
    collections: [
        {
            name: "elements",
            durability: "transient",
        }
    ],
    use: [
        {
            runner: "elf",
        },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            from: "realm",
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
        },
    ],
    children: [
    ],
    program: {
        args: [ "--zarg_first", "zoo_opt", "--arg3", "and_arg3_value" ],
        binary: "bin/session_manager",
    },
}"##;
        let expected = r##"{
  offer: [
    { runner: "elf" },
    {
      from: "framework",
      to: "#elements",
      protocol: "/svc/fuchsia.sys2.Realm"
    },
    {
      to: "#elements",
      protocol: [
        "/svc/fuchsia.cobalt.LoggerFactory",
        "/svc/fuchsia.logger.LogSink"
      ],
      from: "realm"
    }
  ],
  collections: [
    {
      name: "elements",
      durability: "transient"
    }
  ],
  use: [
    { runner: "elf" },
    {
      protocol: "/svc/fuchsia.sys2.Realm",
      from: "framework"
    },
    {
      from: "realm",
      to: "#elements",
      protocol: [
        "/svc/fuchsia.cobalt.LoggerFactory",
        "/svc/fuchsia.logger.LogSink"
      ]
    }
  ],
  children: [],
  program: {
    args: [
      "--arg3",
      "--zarg_first",
      "and_arg3_value",
      "zoo_opt"
    ],
    binary: "bin/session_manager"
  }
}
"##;
        unsafe {
            TEST_ARGS = Some(vec![
                "formatjson5",
                "--replace",
                "--no_trailing_commas",
                "--one_element_lines",
                "--sort_arrays",
                "--indent",
                "2",
                "-",
            ]);
            TEST_BUFFER = Some(example_json5.to_string());
        }
        main().expect("test failed");
        assert!(unsafe { &TEST_BUFFER }.is_some());
        assert_eq!(unsafe { TEST_BUFFER.as_ref().unwrap() }, expected);
    }

    #[test]
    fn test_args() {
        let args = Opt::from_iter(vec![""].iter());
        assert_eq!(args.files.len(), 0);
        assert_eq!(args.replace, false);
        assert_eq!(args.no_trailing_commas, false);
        assert_eq!(args.one_element_lines, false);
        assert_eq!(args.sort_arrays, false);
        assert_eq!(args.indent, 4);

        let some_filename = "some_file.json5";
        let args = Opt::from_iter(
            vec!["formatjson5", "-r", "-n", "-o", "-s", "-i", "2", some_filename].iter(),
        );
        assert_eq!(args.files.len(), 1);
        assert_eq!(args.replace, true);
        assert_eq!(args.no_trailing_commas, true);
        assert_eq!(args.one_element_lines, true);
        assert_eq!(args.sort_arrays, true);
        assert_eq!(args.indent, 2);

        let filename = args.files[0].clone().into_os_string().to_string_lossy().to_string();
        assert_eq!(filename, some_filename);
    }
}
