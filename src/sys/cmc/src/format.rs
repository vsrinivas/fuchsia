// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cm_json::Error;
use serde::ser::Serialize;
use serde_json::ser::{CompactFormatter, PrettyFormatter, Serializer};
use serde_json::Value;
use std::fs;
use std::io::{Read, Write};
use std::path::PathBuf;
use std::str::from_utf8;

/// read in the json file from the given path, minify it if pretty is false or pretty-ify it if
/// pretty is true, and then write the results to stdout if output is None or to the given path if
/// output is Some.
pub fn format(file: &PathBuf, pretty: bool, output: Option<PathBuf>) -> Result<(), Error> {
    let mut buffer = String::new();
    fs::File::open(&file)?.read_to_string(&mut buffer)?;
    let v: Value = serde_json::from_str(&buffer)
        .map_err(|e| Error::parse(format!("Couldn't read input as JSON: {}", e)))?;
    let mut res = Vec::new();
    if pretty {
        let mut ser = Serializer::with_formatter(&mut res, PrettyFormatter::with_indent(b"    "));
        v.serialize(&mut ser)
            .map_err(|e| Error::parse(format!("Couldn't serialize JSON: {}", e)))?;
    } else {
        let mut ser = Serializer::with_formatter(&mut res, CompactFormatter {});
        v.serialize(&mut ser)
            .map_err(|e| Error::parse(format!("Couldn't serialize JSON: {}", e)))?;
    }
    if let Some(output_path) = output {
        fs::OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(output_path)?
            .write_all(&res)?;
    } else {
        println!("{}", from_utf8(&res)?);
    }
    Ok(())
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
        let result = format(&tmp_file_path, false, Some(output_file_path.clone()));
        assert!(result.is_ok());

        let mut buffer = String::new();
        File::open(&output_file_path).unwrap().read_to_string(&mut buffer).unwrap();
        assert_eq!(0, count_num_newlines(&buffer));

        // format as pretty
        let result = format(&tmp_file_path, true, Some(output_file_path.clone()));
        assert!(result.is_ok());

        let mut buffer = String::new();
        File::open(&output_file_path).unwrap().read_to_string(&mut buffer).unwrap();
        assert_eq!(13, count_num_newlines(&buffer));
    }

    #[test]
    fn test_format_invalid_json_fails() {
        let example_json = "{\"foo\": 1,}";
        let tmp_dir = TempDir::new().unwrap();

        let tmp_file_path = tmp_dir.path().join("input.json");
        File::create(&tmp_file_path).unwrap().write_all(example_json.as_bytes()).unwrap();

        // format as not-pretty
        let result = format(&tmp_file_path, false, None);
        assert!(result.is_err());
    }
}
