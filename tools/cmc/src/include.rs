// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error,
    crate::merge::merge_json,
    crate::util::{json_or_json5_from_file, write_depfile},
    serde_json::Value,
    std::{
        collections::{HashSet, VecDeque},
        fs,
        io::{BufRead, BufReader, Write},
        path::PathBuf,
    },
};

/// Read in the provided JSON file and add includes.
/// If the JSON file is an object with a key "include" that references an array of strings then
/// the strings are treated as paths to JSON files to be merged with the input file.
/// Returns any includes encountered.
/// If a depfile is provided, also writes includes encountered to the depfile.
pub fn merge_includes(
    file: &PathBuf,
    output: Option<&PathBuf>,
    depfile: Option<&PathBuf>,
    includepath: &PathBuf,
) -> Result<(), Error> {
    // Recursively collect includes
    let mut v: Value = json_or_json5_from_file(&file)?;
    let mut new_includes = VecDeque::from(extract_includes(&mut v));
    let mut seen_includes = HashSet::new();

    while let Some(new_include) = new_includes.pop_front() {
        // Check for cycles
        if !seen_includes.insert(new_include.clone()) {
            return Err(Error::parse(
                format!("Includes cycle at {}", new_include),
                None,
                Some(&file),
            ));
        }
        // Read include
        let path = includepath.join(new_include);
        let mut includev: Value = json_or_json5_from_file(&path).map_err(|e| {
            Error::parse(
                format!("Couldn't read include {}: {}", &path.display(), e),
                None,
                Some(&file),
            )
        })?;
        new_includes.extend(extract_includes(&mut includev));
        merge_json(&mut v, &includev).map_err(|e| {
            Error::parse(format!("Failed to merge with {:?}: {}", path, e), None, Some(&file))
        })?;
    }

    // Write postprocessed JSON
    if let Some(output_path) = output.as_ref() {
        fs::OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(output_path)?
            .write_all(format!("{:#}", v).as_bytes())?;
    } else {
        println!("{:#}", v);
    }

    // Write includes to depfile
    if let Some(depfile_path) = depfile {
        let mut sorted_includes = seen_includes.into_iter().collect::<Vec<String>>();
        sorted_includes.sort();
        write_depfile(&depfile_path, output, &sorted_includes, &includepath)?;
    }

    Ok(())
}

const CHECK_INCLUDES_URL: &str =
    "https://fuchsia.dev/fuchsia-src/development/components/build#component-manifest-includes";

/// Read in the provided JSON file and ensure that it contains all expected includes.
pub fn check_includes(
    file: &PathBuf,
    mut expected_includes: Vec<String>,
    // If specified, this is a path to newline-delimited `expected_includes`
    fromfile: Option<&PathBuf>,
) -> Result<(), Error> {
    if let Some(path) = fromfile {
        let reader = BufReader::new(fs::File::open(path)?);
        for line in reader.lines() {
            match line {
                Ok(value) => expected_includes.push(String::from(value)),
                Err(e) => return Err(Error::invalid_args(format!("Invalid --fromfile: {}", e))),
            }
        }
    }
    if expected_includes.is_empty() {
        return Ok(()); // Nothing to do
    }

    let mut v: Value = json_or_json5_from_file(&file)?;
    let includes = extract_includes(&mut v);
    for expected in expected_includes {
        if !includes.contains(&expected) {
            return Err(Error::Validate {
                schema_name: None,
                err: format!(
                    "{:?} must include {}.\nSee: {}",
                    &file, &expected, CHECK_INCLUDES_URL
                ),
                filename: file.to_str().map(String::from),
            });
        }
    }
    Ok(())
}

/// Extracts includes from a given JSON document.
/// For instance, if the document is `{ "include": [ "foo", "bar" ], "baz": "qux" }`
/// then this will extract `[ "foo", "bar" ]`
/// and leave behind `{ "baz": "qux" }`.
pub fn extract_includes(doc: &mut Value) -> Vec<String> {
    // Extract includes if present.
    doc.as_object_mut().map_or(vec![], |v| {
        v.remove("include").map_or(vec![], |v| {
            v.as_array().map_or(vec![], |v| {
                v.iter().cloned().filter_map(|v| v.as_str().map(String::from)).collect()
            })
        })
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;
    use serde_json::json;
    use std::fmt::Display;
    use std::fs::File;
    use std::io::{LineWriter, Read};
    use tempfile::TempDir;

    fn tmp_file(tmp_dir: &TempDir, name: &str, contents: impl Display) -> PathBuf {
        let path = tmp_dir.path().join(name);
        File::create(tmp_dir.path().join(name))
            .unwrap()
            .write_all(format!("{:#}", contents).as_bytes())
            .unwrap();
        return path;
    }

    fn assert_eq_file(file: PathBuf, contents: impl Display) {
        let mut out = String::new();
        File::open(file).unwrap().read_to_string(&mut out).unwrap();
        assert_eq!(out, format!("{:#}", contents));
    }

    #[test]
    fn test_include_cmx() {
        let tmp_dir = TempDir::new().unwrap();
        let include_path = tmp_dir.path().to_path_buf();
        let cmx_path = tmp_file(
            &tmp_dir,
            "some.cmx",
            json!({
                "include": ["shard.cmx"],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        tmp_file(
            &tmp_dir,
            "shard.cmx",
            json!({
                "sandbox": {
                    "services": ["fuchsia.foo.Bar"]
                }
            }),
        );

        let out_cmx_path = tmp_dir.path().join("out.cmx");
        let cmx_depfile_path = tmp_dir.path().join("cmx.d");
        merge_includes(&cmx_path, Some(&out_cmx_path), Some(&cmx_depfile_path), &include_path)
            .unwrap();

        assert_eq_file(
            out_cmx_path,
            json!({
                "program": {
                    "binary": "bin/hello_world"
                },
                "sandbox": {
                    "services": ["fuchsia.foo.Bar"]
                }
            }),
        );
        let mut deps = String::new();
        File::open(&cmx_depfile_path).unwrap().read_to_string(&mut deps).unwrap();
        assert_eq!(
            deps,
            format!("{tmp}/out.cmx: {tmp}/shard.cmx\n", tmp = tmp_dir.path().display())
        );
    }

    #[test]
    fn test_include_cml() {
        let tmp_dir = TempDir::new().unwrap();
        let include_path = tmp_dir.path().to_path_buf();
        let cml_path = tmp_file(
            &tmp_dir,
            "some.cml",
            "{include: [\"shard.cml\"], program: {binary: \"bin/hello_world\"}}",
        );
        tmp_file(&tmp_dir, "shard.cml", "{use: [{ protocol: [\"fuchsia.foo.Bar\"]}]}");

        let out_cml_path = tmp_dir.path().join("out.cml");
        let cml_depfile_path = tmp_dir.path().join("cml.d");
        merge_includes(&cml_path, Some(&out_cml_path), Some(&cml_depfile_path), &include_path)
            .unwrap();

        assert_eq_file(
            out_cml_path,
            json!({
                "program": {
                    "binary": "bin/hello_world"
                },
                "use": [{
                    "protocol": ["fuchsia.foo.Bar"]
                }]
            }),
        );
        let mut deps = String::new();
        File::open(&cml_depfile_path).unwrap().read_to_string(&mut deps).unwrap();
        assert_eq!(
            deps,
            format!("{tmp}/out.cml: {tmp}/shard.cml\n", tmp = tmp_dir.path().display())
        );
    }

    #[test]
    fn test_include_multiple_shards() {
        let tmp_dir = TempDir::new().unwrap();
        let include_path = tmp_dir.path().to_path_buf();
        let cmx_path = tmp_file(
            &tmp_dir,
            "some.cmx",
            json!({
                "include": ["shard1.cmx", "shard2.cmx"],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        tmp_file(
            &tmp_dir,
            "shard1.cmx",
            json!({
                "sandbox": {
                    "services": ["fuchsia.foo.Bar"]
                }
            }),
        );
        tmp_file(
            &tmp_dir,
            "shard2.cmx",
            json!({
                "sandbox": {
                    "services": ["fuchsia.foo.Qux"]
                }
            }),
        );

        let out_cmx_path = tmp_dir.path().join("out.cmx");
        let cmx_depfile_path = tmp_dir.path().join("cmx.d");
        merge_includes(&cmx_path, Some(&out_cmx_path), Some(&cmx_depfile_path), &include_path)
            .unwrap();

        assert_eq_file(
            out_cmx_path,
            json!({
                "program": {
                    "binary": "bin/hello_world"
                },
                "sandbox": {
                    "services": ["fuchsia.foo.Bar", "fuchsia.foo.Qux"]
                }
            }),
        );
        let mut deps = String::new();
        File::open(&cmx_depfile_path).unwrap().read_to_string(&mut deps).unwrap();
        assert_eq!(
            deps,
            format!(
                "{tmp}/out.cmx: {tmp}/shard1.cmx {tmp}/shard2.cmx\n",
                tmp = tmp_dir.path().display()
            )
        );
    }

    #[test]
    fn test_include_recursively() {
        let tmp_dir = TempDir::new().unwrap();
        let include_path = tmp_dir.path().to_path_buf();
        let cmx_path = tmp_file(
            &tmp_dir,
            "some.cmx",
            json!({
                "include": ["shard1.cmx"],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        tmp_file(
            &tmp_dir,
            "shard1.cmx",
            json!({
                "include": ["shard2.cmx"],
                "sandbox": {
                    "services": ["fuchsia.foo.Bar"]
                }
            }),
        );
        tmp_file(
            &tmp_dir,
            "shard2.cmx",
            json!({
                "sandbox": {
                    "services": ["fuchsia.foo.Qux"]
                }
            }),
        );

        let out_cmx_path = tmp_dir.path().join("out.cmx");
        let cmx_depfile_path = tmp_dir.path().join("cmx.d");
        merge_includes(&cmx_path, Some(&out_cmx_path), Some(&cmx_depfile_path), &include_path)
            .unwrap();

        assert_eq_file(
            out_cmx_path,
            json!({
                "program": {
                    "binary": "bin/hello_world"
                },
                "sandbox": {
                    "services": ["fuchsia.foo.Bar", "fuchsia.foo.Qux"]
                }
            }),
        );
        let mut deps = String::new();
        File::open(&cmx_depfile_path).unwrap().read_to_string(&mut deps).unwrap();
        assert_eq!(
            deps,
            format!(
                "{tmp}/out.cmx: {tmp}/shard1.cmx {tmp}/shard2.cmx\n",
                tmp = tmp_dir.path().display()
            )
        );
    }

    #[test]
    fn test_include_nothing() {
        let tmp_dir = TempDir::new().unwrap();
        let include_path = tmp_dir.path().to_path_buf();
        let cmx_path = tmp_file(
            &tmp_dir,
            "some.cmx",
            json!({
                "include": [],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );

        let out_cmx_path = tmp_dir.path().join("out.cmx");
        let cmx_depfile_path = tmp_dir.path().join("cmx.d");
        merge_includes(&cmx_path, Some(&out_cmx_path), Some(&cmx_depfile_path), &include_path)
            .unwrap();

        assert_eq_file(
            out_cmx_path,
            json!({
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_eq!(cmx_depfile_path.exists(), false);
    }

    #[test]
    fn test_no_includes() {
        let tmp_dir = TempDir::new().unwrap();
        let include_path = tmp_dir.path().to_path_buf();
        let cmx_path = tmp_file(
            &tmp_dir,
            "some.cmx",
            json!({
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );

        let out_cmx_path = tmp_dir.path().join("out.cmx");
        let cmx_depfile_path = tmp_dir.path().join("cmx.d");
        merge_includes(&cmx_path, Some(&out_cmx_path), Some(&cmx_depfile_path), &include_path)
            .unwrap();

        assert_eq_file(
            out_cmx_path,
            json!({
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_eq!(cmx_depfile_path.exists(), false);
    }

    #[test]
    fn test_invalid_include() {
        let tmp_dir = TempDir::new().unwrap();
        let include_path = tmp_dir.path().to_path_buf();
        let cmx_path = tmp_file(
            &tmp_dir,
            "some.cmx",
            json!({
                "include": ["doesnt_exist.cmx"],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );

        let out_cmx_path = tmp_dir.path().join("out.cmx");
        let cmx_depfile_path = tmp_dir.path().join("cmx.d");
        let result =
            merge_includes(&cmx_path, Some(&out_cmx_path), Some(&cmx_depfile_path), &include_path);

        assert_matches!(result, Err(Error::Parse { err, .. })
                        if err.starts_with("Couldn't read include ") && err.contains("doesnt_exist.cmx"));
    }

    #[test]
    fn test_include_cycle() {
        let tmp_dir = TempDir::new().unwrap();
        let include_path = tmp_dir.path().to_path_buf();
        let cmx_path = tmp_file(
            &tmp_dir,
            "some1.cmx",
            json!({
                "include": ["some2.cmx"],
            }),
        );
        tmp_file(
            &tmp_dir,
            "some2.cmx",
            json!({
                "include": ["some1.cmx"],
            }),
        );

        let out_cmx_path = tmp_dir.path().join("out.cmx");
        let result = merge_includes(&cmx_path, Some(&out_cmx_path), None, &include_path);
        assert_matches!(result, Err(Error::Parse { err, .. }) if err.contains("Includes cycle"));
    }

    #[test]
    fn test_expect_nothing() {
        let tmp_dir = TempDir::new().unwrap();
        let cmx1_path = tmp_file(
            &tmp_dir,
            "some1.cmx",
            json!({
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_matches!(check_includes(&cmx1_path, vec![], None), Ok(()));

        let cmx2_path = tmp_file(
            &tmp_dir,
            "some2.cmx",
            json!({
                "include": [],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_matches!(check_includes(&cmx2_path, vec![], None), Ok(()));

        let cmx3_path = tmp_file(
            &tmp_dir,
            "some3.cmx",
            json!({
                "include": [ "foo.cmx" ],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_matches!(check_includes(&cmx3_path, vec![], None), Ok(()));
    }

    #[test]
    fn test_expect_something_present() {
        let tmp_dir = TempDir::new().unwrap();
        let cmx_path = tmp_file(
            &tmp_dir,
            "some.cmx",
            json!({
                "include": [ "foo.cmx", "bar.cmx" ],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_matches!(check_includes(&cmx_path, vec!["bar.cmx".into()], None), Ok(()));
    }

    #[test]
    fn test_expect_something_missing() {
        let tmp_dir = TempDir::new().unwrap();
        let cmx1_path = tmp_file(
            &tmp_dir,
            "some1.cmx",
            json!({
                "include": [ "foo.cmx", "bar.cmx" ],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_matches!(check_includes(&cmx1_path, vec!["qux.cmx".into()], None),
                        Err(Error::Validate { filename, .. }) if filename == cmx1_path.to_str().map(String::from));
        let cmx2_path = tmp_file(
            &tmp_dir,
            "some2.cmx",
            json!({
                // No includes
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_matches!(check_includes(&cmx2_path, vec!["qux.cmx".into()], None),
                        Err(Error::Validate { filename, .. }) if filename == cmx2_path.to_str().map(String::from));
    }

    #[test]
    fn test_expect_fromfile() {
        let tmp_dir = TempDir::new().unwrap();
        let cmx_path = tmp_file(
            &tmp_dir,
            "some.cmx",
            json!({
                "include": [ "foo.cmx", "bar.cmx" ],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );

        let fromfile_path = tmp_dir.path().join("fromfile");
        let mut fromfile = LineWriter::new(File::create(fromfile_path.clone()).unwrap());
        writeln!(fromfile, "foo.cmx").unwrap();
        writeln!(fromfile, "bar.cmx").unwrap();
        assert_matches!(check_includes(&cmx_path, vec![], Some(&fromfile_path)), Ok(()));

        // Add another include that's missing
        writeln!(fromfile, "qux.cmx").unwrap();
        assert_matches!(check_includes(&cmx_path, vec![], Some(&fromfile_path)),
                        Err(Error::Validate { filename, .. }) if filename == cmx_path.to_str().map(String::from));
    }
}
