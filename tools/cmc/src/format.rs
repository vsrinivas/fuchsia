// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::Error;
use crate::util;
use cml::format_cml;
use serde::ser::Serialize;
use serde_json::ser::{CompactFormatter, PrettyFormatter, Serializer};
use std::fs;
use std::io::{Read, Write};
use std::path::PathBuf;
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
        format_cml(&buffer, &file)?
    } else {
        format_cmx(buffer, pretty)?
    };

    if let Some(output_path) = output {
        util::ensure_directory_exists(&output_path)?;
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

pub fn format_cmx(buffer: String, pretty: bool) -> Result<Vec<u8>, Error> {
    let v: serde_json::Value = serde_json::from_str(&buffer)?;
    let mut res = Vec::new();
    if pretty {
        let mut ser = Serializer::with_formatter(&mut res, PrettyFormatter::with_indent(b"    "));
        v.serialize(&mut ser)?;
    } else {
        let mut ser = Serializer::with_formatter(&mut res, CompactFormatter {});
        v.serialize(&mut ser)?;
    }
    Ok(res)
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
            protocol: "/svc/fuchsia.component.Realm",
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
            protocol: "/svc/fuchsia.component.Realm",
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
        runner: "elf",
    },
}"##;
        let expected = r##"{
    program: {
        runner: "elf",
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
        {
            protocol: "/svc/fuchsia.component.Realm",
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
            protocol: "/svc/fuchsia.component.Realm",
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
    fn test_format_cml_with_environments() {
        let example_json5 = r##"{
    include: [ "src/sys/test_manager/meta/common.shard.cml" ],
    environments: [
        {
            name: "test-env",
            extends: "realm",
            runners: [
                {
                    from: "#elf_test_runner",
                    runner: "elf_test_runner",
                },
                {
                    from: "#gtest_runner",
                    runner: "gtest_runner",
                },
                {
                    from: "#rust_test_runner",
                    runner: "rust_test_runner",
                },
                {
                    from: "#go_test_runner",
                    runner: "go_test_runner",
                },
                {
                    from: "#fuchsia_component_test_framework_intermediary",
                    runner: "fuchsia_component_test_mocks",
                },
            ],
            resolvers: [
                {
                    from: "#fuchsia_component_test_framework_intermediary",
                    resolver: "fuchsia_component_test_registry",
                    scheme: "fuchsia-component-test-registry",
                },
            ],
        },
    ],
}
"##;
        let expected = r##"{
    include: [ "src/sys/test_manager/meta/common.shard.cml" ],
    environments: [
        {
            name: "test-env",
            extends: "realm",
            runners: [
                {
                    runner: "elf_test_runner",
                    from: "#elf_test_runner",
                },
                {
                    runner: "gtest_runner",
                    from: "#gtest_runner",
                },
                {
                    runner: "rust_test_runner",
                    from: "#rust_test_runner",
                },
                {
                    runner: "go_test_runner",
                    from: "#go_test_runner",
                },
                {
                    runner: "fuchsia_component_test_mocks",
                    from: "#fuchsia_component_test_framework_intermediary",
                },
            ],
            resolvers: [
                {
                    resolver: "fuchsia_component_test_registry",
                    from: "#fuchsia_component_test_framework_intermediary",
                    scheme: "fuchsia-component-test-registry",
                },
            ],
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
