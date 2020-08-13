// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::{Error, Location};
use json5format;
use json5format::{FormatOptions, PathOption};
use maplit::hashmap;
use maplit::hashset;
use serde::ser::Serialize;
use serde_json::ser::{CompactFormatter, PrettyFormatter, Serializer};
use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::str::from_utf8;

/// For `.cml` JSON5 files, format the file to match the default style. (The "pretty" option is
/// ignored.) See format_cml() for current style conventions.
///
/// For all other files, assume the input is standard JSON:
/// Read in the json file from the given path, minify it if pretty is false or pretty-ify it if
/// pretty is true, and then write the results to stdout if output is None or to the given path if
/// output is Some.
pub fn format(
    file: &PathBuf,
    pretty: bool,
    cml: bool,
    output: Option<PathBuf>,
) -> Result<(), Error> {
    let mut buffer = String::new();
    fs::File::open(&file)?.read_to_string(&mut buffer)?;

    let file_path = file
        .clone()
        .into_os_string()
        .into_string()
        .map_err(|e| Error::internal(format!("Unhandled file path: {:?}", e)))?;

    let res = if cml || file_path.ends_with(".cml") {
        format_cml(buffer, file.as_path())?
    } else {
        format_cmx(buffer, file.as_path(), pretty)?
    };

    if let Some(output_path) = output {
        fs::OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(output_path)?
            .write_all(&res)?;
    } else {
        // Print without a newline because the formatters should have already added a final newline
        // (required by most software style guides).
        print!("{}", from_utf8(&res)?);
    }
    Ok(())
}

pub fn format_cmx(buffer: String, file: &Path, pretty: bool) -> Result<Vec<u8>, Error> {
    let v: serde_json::Value = serde_json::from_str(&buffer).map_err(|e| {
        Error::parse(
            format!("Couldn't read input as JSON: {}", e),
            Some(Location { line: e.line(), column: e.column() }),
            Some(file),
        )
    })?;
    let mut res = Vec::new();
    if pretty {
        let mut ser = Serializer::with_formatter(&mut res, PrettyFormatter::with_indent(b"    "));
        v.serialize(&mut ser).map_err(|e| {
            Error::parse(
                format!("Couldn't serialize JSON: {}", e),
                Some(Location { line: e.line(), column: e.column() }),
                Some(file),
            )
        })?;
    } else {
        let mut ser = Serializer::with_formatter(&mut res, CompactFormatter {});
        v.serialize(&mut ser).map_err(|e| {
            Error::parse(
                format!("Couldn't serialize JSON: {}", e),
                Some(Location { line: e.line(), column: e.column() }),
                Some(file),
            )
        })?;
    }
    Ok(res)
}

pub fn format_cml(buffer: String, file: &Path) -> Result<Vec<u8>, Error> {
    let options = FormatOptions {
        collapse_containers_of_one: true,
        sort_array_items: true, // but use options_by_path to turn this off for program args
        options_by_path: hashmap! {
            "/*" => hashset! {
                PathOption::PropertyNameOrder(vec![
                    "program",
                    "children",
                    "collections",
                    "capabilities",
                    "use",
                    "offer",
                    "expose",
                    "environments",
                    "facets",
                ])
            },
            "/*/program" => hashset! {
                PathOption::CollapseContainersOfOne(false),
                PathOption::PropertyNameOrder(vec![
                    "binary",
                    "args",
                ])
            },
            "/*/program/args" => hashset! {
                PathOption::SortArrayItems(false),
            },
            "/*/*/*" => hashset! {
                PathOption::PropertyNameOrder(vec![
                    "name",
                    "url",
                    "startup",
                    "environment",
                    "durability",
                    "service",
                    "protocol",
                    "directory",
                    "storage",
                    "runner",
                    "resolver",
                    "event",
                    "event_stream",
                    "from",
                    "as",
                    "to",
                    "rights",
                    "path",
                    "subdir",
                    "filter",
                    "dependency",
                    "extends",
                    "runners",
                    "resolvers",
                ])
            },
        },
        ..Default::default()
    };

    json5format::format(&buffer, Some(file.to_string_lossy().into_owned()), Some(options)).map_err(
        |err| match err {
            json5format::Error::Configuration(errstr) => Error::Internal(errstr),
            json5format::Error::Parse(location, errstr) => match location {
                Some(location) => Error::parse(
                    errstr,
                    Some(Location { line: location.line, column: location.col }),
                    Some(file),
                ),
                None => Error::parse(errstr, None, Some(file)),
            },
            json5format::Error::Internal(location, errstr) => match location {
                Some(location) => Error::Internal(format!("{}: {}", location, errstr)),
                None => Error::Internal(errstr),
            },
            json5format::Error::TestFailure(location, errstr) => match location {
                Some(location) => {
                    Error::Internal(format!("{}: Test failure: {}", location, errstr))
                }
                None => Error::Internal(format!("Test failure: {}", errstr)),
            },
        },
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::File;
    use std::io::Write;
    use tempfile::TempDir;

    fn count_num_newlines(input: &str) -> usize {
        let mut ret = 0;
        for c in input.chars() {
            if c == '\n' {
                ret += 1;
            }
        }
        ret
    }

    #[test]
    fn test_format_json() {
        let example_json = r#"{ "program": { "binary": "bin/app"
}, "sandbox": {
"dev": [
"class/camera" ], "features": [
 "persistent-storage", "vulkan"] } }"#;
        let tmp_dir = TempDir::new().unwrap();

        let tmp_file_path = tmp_dir.path().join("input.json");
        File::create(&tmp_file_path).unwrap().write_all(example_json.as_bytes()).unwrap();

        let output_file_path = tmp_dir.path().join("output.json");

        // format as not-pretty
        let result = format(&tmp_file_path, false, false, Some(output_file_path.clone()));
        assert!(result.is_ok());

        let mut buffer = String::new();
        File::open(&output_file_path).unwrap().read_to_string(&mut buffer).unwrap();
        assert_eq!(0, count_num_newlines(&buffer));

        // format as pretty (not .cml)
        let result = format(&tmp_file_path, true, false, Some(output_file_path.clone()));
        assert!(result.is_ok());

        let mut buffer = String::new();
        File::open(&output_file_path).unwrap().read_to_string(&mut buffer).unwrap();
        assert_eq!(13, count_num_newlines(&buffer));
    }

    #[test]
    fn test_format_json5() {
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
            durability: "transient",
            name: "elements",
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
    program: {
        binary: "bin/session_manager",
        args: [
            "--zarg_first",
            "zoo_opt",
            "--arg3",
            "and_arg3_value",
        ],
    },
    children: [],
    collections: [
        {
            name: "elements",
            durability: "transient",
        },
    ],
    use: [
        { runner: "elf" },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            protocol: [
                "/svc/fuchsia.cobalt.LoggerFactory",
                "/svc/fuchsia.logger.LogSink",
            ],
            from: "realm",
            to: "#elements",
        },
    ],
    offer: [
        { runner: "elf" },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
            to: "#elements",
        },
        {
            protocol: [
                "/svc/fuchsia.cobalt.LoggerFactory",
                "/svc/fuchsia.logger.LogSink",
            ],
            from: "realm",
            to: "#elements",
        },
    ],
}
"##;
        let tmp_dir = TempDir::new().unwrap();

        let tmp_file_path = tmp_dir.path().join("input.cml");
        File::create(&tmp_file_path).unwrap().write_all(example_json5.as_bytes()).unwrap();

        let output_file_path = tmp_dir.path().join("output.cml");

        // format as json5 with .cml style options
        let result = format(&tmp_file_path, false, true, Some(output_file_path.clone()));
        assert!(result.is_ok());

        let mut buffer = String::new();
        File::open(&output_file_path).unwrap().read_to_string(&mut buffer).unwrap();
        assert_eq!(buffer, expected);
    }

    #[test]
    fn test_format_invalid_json_fails() {
        let example_json = "{\"foo\": 1,}";
        let tmp_dir = TempDir::new().unwrap();

        let tmp_file_path = tmp_dir.path().join("input.json");
        File::create(&tmp_file_path).unwrap().write_all(example_json.as_bytes()).unwrap();

        // format as not-pretty (not .cml)
        let result = format(&tmp_file_path, false, false, None);
        assert!(result.is_err());
    }

    #[test]
    fn test_format_invalid_json_may_be_valid_json5() {
        let example_json = "{\"foo\": 1,}";
        let tmp_dir = TempDir::new().unwrap();

        let tmp_file_path = tmp_dir.path().join("input.json");
        File::create(&tmp_file_path).unwrap().write_all(example_json.as_bytes()).unwrap();

        // json formatter
        let result = format(&tmp_file_path, false, false, None);
        assert!(result.is_err());
        // json5 formatter
        let result = format(&tmp_file_path, false, true, None);
        assert!(result.is_ok());
    }

    #[test]
    fn test_format_invalid_json5_fails() {
        let example_json5 = "{\"foo\" 1}";
        let tmp_dir = TempDir::new().unwrap();

        let tmp_file_path = tmp_dir.path().join("input.json");
        File::create(&tmp_file_path).unwrap().write_all(example_json5.as_bytes()).unwrap();

        // json5 formatter
        let result = format(&tmp_file_path, false, true, None);
        assert!(result.is_err());
    }
}
