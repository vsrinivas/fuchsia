// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::{Error, Location},
    serde_json::{json, Value},
    serde_json5,
    std::fs,
    std::io::{Read, Write},
    std::path::PathBuf,
};

/// read in the provided list of json files, merge them, and pretty-print the merged result to
/// stdout if output is None or to the provided path if output is Some. JSON objects are merged
/// recursively, and if two blobs set the same key an error is returned. JSON arrays are appended
/// together, with duplicate items being removed.
pub fn merge(files: Vec<PathBuf>, output: Option<PathBuf>) -> Result<(), Error> {
    if files.is_empty() {
        return Err(Error::invalid_args(format!("no files provided")));
    }
    let mut res = json!({});
    for filename in files {
        let mut buffer = String::new();
        fs::File::open(&filename)?.read_to_string(&mut buffer)?;

        let v: Value = match serde_json::from_str(&buffer) {
            Ok(value) => value,
            Err(_) => {
                // If JSON parsing fails, try JSON5 parsing (which is slower)
                serde_json5::from_str(&buffer).map_err(|e| {
                    let serde_json5::Error::Message { ref location, .. } = e;
                    let location =
                        location.as_ref().map(|l| Location { line: l.line, column: l.column });
                    Error::parse(
                        format!("Couldn't read input as JSON: {}", e),
                        location,
                        Some(filename.as_path()),
                    )
                })?
            }
        };

        merge_json(&mut res, &v).map_err(|e| {
            Error::parse(
                format!("Multiple manifests set the same key: {}", e),
                None,
                Some(filename.as_path()),
            )
        })?;
    }
    if let Some(output_path) = output {
        fs::OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(output_path)?
            .write_all(format!("{:#}", res).as_bytes())?;
    } else {
        println!("{:#}", res);
    }
    Ok(())
}

/// Merges the JSON values in `from` into the object in `res`.
///
/// If `from` is an array of objects, each object in the array will be merged
/// into `res` one at a time, in order. If `from` is an object, that object will
/// be merged into `res`.
fn merge_json(mut res: &mut Value, from: &Value) -> Result<(), String> {
    match &from {
        Value::Array(from_arr) => {
            for item in from_arr {
                merge_json_inner(&mut res, &item)?;
            }
        }
        Value::Object(_) => merge_json_inner(&mut res, &from)?,
        _ => return Err("files to be merged must contain an object or an array of objects".into()),
    }
    Ok(())
}

fn merge_json_inner(mut res: &mut Value, from: &Value) -> Result<(), String> {
    match (&mut res, &from) {
        (Value::Object(res_map), Value::Object(from_map)) => {
            for (k, v) in from_map {
                if !res_map.contains_key(k) {
                    res_map.insert(k.clone(), v.clone());
                } else {
                    merge_json_inner(&mut res_map[k], v).map_err(|e| {
                        if e == "" {
                            format!("{}", k)
                        } else {
                            format!("{}.{}", k, e)
                        }
                    })?;
                }
            }
        }
        (Value::Array(res_arr), Value::Array(from_arr)) => {
            for item in from_arr {
                if !res_arr.contains(&item) {
                    res_arr.push(item.clone())
                }
            }
        }
        _ => return Err(format!("could not merge `{}` with `{}`", res, from)),
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
    fn test_merge_json() {
        let tests = vec![
            // Valid merges
            (vec![json!({}), json!({})], Some(json!({}))),
            (vec![json!({}), json!([])], Some(json!({}))),
            (vec![json!({}), json!([{}])], Some(json!({}))),
            (vec![json!([]), json!([])], Some(json!({}))),
            (vec![json!([{}]), json!([{}])], Some(json!({}))),
            (vec![json!({"foo": 1}), json!({})], Some(json!({"foo": 1}))),
            (vec![json!({}), json!({"foo": 1})], Some(json!({"foo": 1}))),
            (vec![json!({"foo": 1}), json!({"bar": 2})], Some(json!({"foo": 1, "bar": 2}))),
            (vec![json!({"foo": [1]}), json!({"bar": [2]})], Some(json!({"foo": [1], "bar": [2]}))),
            (
                vec![json!({"foo": {"bar": 1}}), json!({"foo": {"baz": 2}})],
                Some(json!({"foo": {"bar": 1, "baz": 2}})),
            ),
            (vec![json!({"foo": [1]}), json!({"foo": [2]})], Some(json!({"foo": [1,2]}))),
            (vec![json!({"foo": [1]}), json!({"foo": [1]})], Some(json!({"foo": [1]}))),
            (
                vec![json!({"foo": [{"bar": 1}]}), json!({"foo": [{"bar": 1}]})],
                Some(json!({"foo": [{"bar": 1}]})),
            ),
            (
                vec![json!({"foo": [{"bar": 1}]}), json!({"foo": [{"bar": 2}]})],
                Some(json!({"foo": [{"bar": 1},{"bar": 2}]})),
            ),
            (
                vec![json!({"foo": [{"bar": 1}]}), json!([{"foo": [{"bar": 2}]}, {"baz": 3}])],
                Some(json!({"foo": [{"bar": 1},{"bar": 2}], "baz": 3})),
            ),
            // merges that should fail
            (vec![json!({"foo": 1}), json!({"foo": 1})], None),
            (vec![json!({"foo": 1}), json!({"foo": 2})], None),
            (vec![json!({"foo": {"bar": 1}}), json!({"foo": 2})], None),
            (vec![json!({"foo": [1]}), json!({"foo": 1})], None),
            (vec![json!({"foo": [1]}), json!({"foo": {"bar": 1}})], None),
            (vec![json!({"foo": [1]}), json!([{"foo": [2]}, {"foo": 3}])], None),
        ];

        for (vec_to_merge, expected_results) in tests {
            let tmp_dir = TempDir::new().unwrap();

            let mut counter = 0;
            let mut filenames = vec![];
            for json_val in &vec_to_merge {
                let tmp_file_path = tmp_dir.path().join(format!("{}.json", counter));
                counter += 1;
                File::create(&tmp_file_path)
                    .unwrap()
                    .write_all(format!("{}", json_val).as_bytes())
                    .unwrap();
                filenames.push(tmp_file_path);
            }

            let output_file_path = tmp_dir.path().join("output.json");

            let result = merge(filenames, Some(output_file_path.clone()));

            if result.is_ok() != expected_results.is_some() {
                println!("example failed:");
                for item in &vec_to_merge {
                    println!(" - {}", item);
                }
                println!("result={:?}; expected={:?}", result, expected_results);
            }
            assert_eq!(result.is_ok(), expected_results.is_some());

            if let Some(expected_json) = expected_results {
                let mut buffer = String::new();
                File::open(&output_file_path).unwrap().read_to_string(&mut buffer).unwrap();
                assert_eq!(buffer, format!("{:#}", expected_json));
            }
        }
    }

    #[test]
    fn test_merge_json5() {
        let tmp_dir = TempDir::new().unwrap();

        let input = vec![
            (tmp_dir.path().join("1.json"), "{\"foo\": 1,} // comment"),
            (tmp_dir.path().join("2.json"), "{\"bar\": 2,} // comment"),
        ];
        let mut filenames = vec![];
        for (fname, contents) in &input {
            File::create(fname).unwrap().write_all(contents.as_bytes()).unwrap();
            filenames.push(fname.clone());
        }

        let output_file_path = tmp_dir.path().join("output.json");
        merge(filenames, Some(output_file_path.clone())).expect("failed to merge");

        let mut buffer = String::new();
        File::open(&output_file_path).unwrap().read_to_string(&mut buffer).unwrap();
        let expected_json = json!({"foo": 1, "bar": 2});
        assert_eq!(buffer, format!("{:#}", expected_json));
    }

    #[test]
    fn test_merge_invalid_json_fails() {
        let tmp_dir = TempDir::new().unwrap();

        let input = vec![
            (tmp_dir.path().join("1.json"), "{\"foo\": 1}"),
            (tmp_dir.path().join("2.json"), "{\"foo\": 1,}"),
        ];
        let mut filenames = vec![];
        for (fname, contents) in &input {
            File::create(fname).unwrap().write_all(contents.as_bytes()).unwrap();
            filenames.push(fname.clone());
        }

        let result = merge(filenames, None);
        assert!(result.is_err());
    }
}
