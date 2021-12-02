// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error,
    crate::util,
    crate::util::{json_or_json5_from_file, write_depfile},
    serde_json::{json, Value},
    std::fs,
    std::io::{BufRead, BufReader, Write},
    std::path::PathBuf,
};

/// read in the provided list of json files, merge them, and pretty-print the merged result to
/// stdout if output is None or to the provided path if output is Some.
/// Files can be specified in `files`, or in the contents of `fromfile` as line-delimited paths,
/// or both.
/// JSON objects are merged recursively, and if two blobs set the same key an error is returned.
/// JSON arrays are appended together, with duplicate items being removed.
/// If a depfile is provided, also writes the files encountered to the depfile.
pub fn merge(
    mut files: Vec<PathBuf>,
    output: Option<PathBuf>,
    // If specified, this is a path to newline-delimited `files`
    fromfile: Option<PathBuf>,
    depfile: Option<PathBuf>,
) -> Result<(), Error> {
    if let Some(path) = &fromfile {
        let reader = BufReader::new(fs::File::open(path).map_err(|e| {
            Error::invalid_args(format!("Failed to open --fromfile \"{:?}\": {}", path, e))
        })?);
        for line in reader.lines() {
            match line {
                Ok(value) => files.push(PathBuf::from(value)),
                Err(e) => return Err(Error::invalid_args(format!("Invalid --fromfile: {}", e))),
            }
        }
    }
    if files.is_empty() {
        return Err(Error::invalid_args(format!("no files provided")));
    }
    let mut res = json!({});
    for filename in &files {
        let v: Value = json_or_json5_from_file(filename)?;
        merge_json(&mut res, &v).map_err(|e| {
            Error::parse(
                format!("Multiple manifests set the same key: {}", e),
                None,
                Some(filename.as_path()),
            )
        })?;
    }
    if let Some(output_path) = &output {
        util::ensure_directory_exists(output_path)?;
        fs::OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(output_path)?
            .write_all(format!("{:#}", res).as_bytes())?;
    } else {
        println!("{:#}", res);
    }

    // Write files to depfile
    if let Some(depfile_path) = depfile {
        write_depfile(&depfile_path, output.as_ref(), &files)?;
    }
    Ok(())
}

/// Merges the JSON values in `from` into the object in `res`.
///
/// If `from` is an array of objects, each object in the array will be merged
/// into `res` one at a time, in order. If `from` is an object, that object will
/// be merged into `res`.
pub fn merge_json(mut res: &mut Value, from: &Value) -> Result<(), String> {
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
    use std::io::{LineWriter, Read, Write};
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

            let result = merge(filenames, Some(output_file_path.clone()), None, None);

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
        merge(filenames, Some(output_file_path.clone()), None, None).expect("failed to merge");

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

        let result = merge(filenames, None, None, None);
        assert!(result.is_err());
    }

    #[test]
    fn test_merge_fromfile() {
        let tmp_dir = TempDir::new().unwrap();

        let input = vec![
            // The first two files will be provided as regular inputs
            (tmp_dir.path().join("1.json"), "{\"foo\": 1,} // comment"),
            (tmp_dir.path().join("2.json"), "{\"bar\": 2,} // comment"),
            // The third file will be referenced via --fromfile
            (tmp_dir.path().join("3.json"), "{\"qux\": 3,} // comment"),
        ];
        for (fname, contents) in &input {
            File::create(fname).unwrap().write_all(contents.as_bytes()).unwrap();
        }
        let mut filenames = vec![];
        for (fname, _) in &input[..2] {
            filenames.push(fname.clone());
        }
        let fromfile_path = tmp_dir.path().join("fromfile");
        let mut fromfile = LineWriter::new(File::create(fromfile_path.clone()).unwrap());
        writeln!(fromfile, "{}", input[2].0.clone().into_os_string().into_string().unwrap())
            .unwrap();

        let output_file_path = tmp_dir.path().join("output.json");
        merge(filenames, Some(output_file_path.clone()), Some(fromfile_path), None)
            .expect("failed to merge");

        let mut buffer = String::new();
        File::open(&output_file_path).unwrap().read_to_string(&mut buffer).unwrap();
        let expected_json = json!({"foo": 1, "bar": 2, "qux": 3});
        assert_eq!(buffer, format!("{:#}", expected_json));
    }
}
