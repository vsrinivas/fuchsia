// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error};
use serde_json::Value;
use std::fs;
use std::io::Read;
use std::path::PathBuf;
use valico::json_schema;

/// read in and parse a list of files, and return an Error if any of the given files are not valid
/// cmx. The jsonschema file located at ../schema.json is used to determine the validity of the cmx
/// files.
pub fn validate(files: Vec<PathBuf>) -> Result<(), Error> {
    if files.is_empty() {
        return Err(format_err!("no files provided"));
    }

    for filename in files {
        let mut buffer = String::new();
        fs::File::open(&filename)?.read_to_string(&mut buffer)?;
        let v: Value = serde_json::from_str(&buffer)?;
        validate_json(&v)?;
    }
    Ok(())
}

fn validate_json(json: &Value) -> Result<(), Error> {
    // Parse the schema
    let cmx_schema_string = include_str!("../schema.json");
    let cmx_schema_json = serde_json::from_str(cmx_schema_string)?;
    let mut scope = json_schema::Scope::new();
    let schema = scope
        .compile_and_return(cmx_schema_json, false)
        .map_err(|e| format_err!("couldn't parse schema: {:?}", e))?;

    // Validate the json
    let res = schema.validate(json);
    if !res.is_strictly_valid() {
        let mut err_msgs = Vec::new();
        for e in &res.errors {
            err_msgs.push(format!("{} at {}", e.get_title(), e.get_path()).into_boxed_str());
        }
        // The ordering in which valico emits these errors is unstable.
        // Sort error messages so that the resulting message is predictable.
        err_msgs.sort_unstable();
        return Err(format_err!("{}", err_msgs.join(", ")));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use std::fs::File;
    use std::io::Write;
    use tempfile::TempDir;

    #[test]
    fn test_validate_json() {
        let tests = vec![
            (json!({}), Err(format_err!("This property is required at /program"))),
            (json!({"program": {}}), Err(format_err!("OneOf conditions are not met at /program"))),
            (json!({"program": { "binary": "bin/app" }}), Ok(())),
            (
                json!({"prigram": { "binary": "bin/app" }}),
                Err(format_err!(
                    "Property conditions are not met at , This property is required at /program"))
            ),
            (
                json!({
                    "program": { "binary": "bin/app" },
                    "sandbox": { "dev": [ "class/camera" ] }
                }),
                Ok(())
            ),
            (
                json!({
                    "program": { "binary": "bin/app" },
                    "facets": {
                        "fuchsia.test": {
                            "system-services": [ "fuchsia.net.LegacySocketProvider" ]
                        }
                    }
                }),
                Ok(())
            ),
        ];

        for (input, expected_result) in tests {
            let tmp_dir = TempDir::new().unwrap();
            let tmp_file_path = tmp_dir.path().join("test.cmx");

            File::create(&tmp_file_path)
                .unwrap()
                .write_all(format!("{}", input).as_bytes())
                .unwrap();

            let result = validate(vec![tmp_file_path]);

            assert_eq!(format!("{:?}", result), format!("{:?}", expected_result));
        }
    }

    #[test]
    fn test_validate_invalid_json_fails() {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_file_path = tmp_dir.path().join("test.cmx");

        File::create(&tmp_file_path)
            .unwrap()
            .write_all("{\"program\": { \"binary\": \"bin/app\",}}".as_bytes())
            .unwrap();

        let result = validate(vec![tmp_file_path]);
        assert!(result.is_err());
    }
}
