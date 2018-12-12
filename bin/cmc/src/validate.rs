// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{Error, CMX_SCHEMA};
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
        return Err(Error::invalid_args("no files provided"));
    }

    for filename in files {
        let mut buffer = String::new();
        fs::File::open(&filename)?.read_to_string(&mut buffer)?;
        let v: Value = serde_json::from_str(&buffer)
            .map_err(|e| Error::parse(format!("Couldn't read input as JSON: {}", e)))?;
        validate_json(&v)?;
    }
    Ok(())
}

fn validate_json(json: &Value) -> Result<(), Error> {
    // Parse the schema
    let cmx_schema_json = serde_json::from_str(CMX_SCHEMA)
        .map_err(|e| Error::internal(format!("Couldn't read schema as JSON: {}", e)))?;
    let mut scope = json_schema::Scope::new();
    let schema = scope
        .compile_and_return(cmx_schema_json, false)
        .map_err(|e| Error::internal(format!("Couldn't parse schema: {:?}", e)))?;

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
        return Err(Error::parse(err_msgs.join(", ")));
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

    macro_rules! test_validate_cmx {
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
                    validate_test("test.cmx", $input, $result);
                }
            )+
        }
    }

    fn validate_test(
        filename: &str, input: serde_json::value::Value, expected_result: Result<(), Error>,
    ) {
        let tmp_dir = TempDir::new().unwrap();
        let tmp_file_path = tmp_dir.path().join(filename);

        File::create(&tmp_file_path)
            .unwrap()
            .write_all(format!("{}", input).as_bytes())
            .unwrap();

        let result = validate(vec![tmp_file_path]);
        assert_eq!(format!("{:?}", result), format!("{:?}", expected_result));
    }

    test_validate_cmx! {
        test_cmx_err_empty_json => {
            input = json!({}),
            result = Err(Error::parse("This property is required at /program")),
        },
        test_cmx_program => {
            input = json!({"program": { "binary": "bin/app" }}),
            result = Ok(()),
        },
        test_cmx_program_no_binary => {
            input = json!({ "program": {}}),
            result = Err(Error::parse("OneOf conditions are not met at /program")),
        },
        test_cmx_bad_program => {
            input = json!({"prigram": { "binary": "bin/app" }}),
            result = Err(Error::parse("Property conditions are not met at , \
                                       This property is required at /program")),
        },
        test_cmx_sandbox => {
            input = json!({
                "program": { "binary": "bin/app" },
                "sandbox": { "dev": [ "class/camera" ] }
            }),
            result = Ok(()),
        },
        test_cmx_facets => {
            input = json!({
                "program": { "binary": "bin/app" },
                "facets": {
                    "fuchsia.test": {
                         "system-services": [ "fuchsia.logger.LogSink" ]
                    }
                }
            }),
            result = Ok(()),
        },
    }
}
