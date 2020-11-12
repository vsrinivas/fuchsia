// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error,
    crate::merge::merge_json,
    crate::util::json_or_json5_from_file,
    serde_json::Value,
    std::{fs, io::Write, path::PathBuf},
};

/// Read in the provided JSON file and add includes.
/// If the JSON file is an object with a key "include" that references an array of strings then
/// the strings are treated as paths to JSON files to be merged with the input file.
/// Any includes encountered are listed in the depfile, delimited by newlines.
pub fn merge_includes(
    file: PathBuf,
    output: Option<PathBuf>,
    depfile: Option<PathBuf>,
    includepath: PathBuf,
) -> Result<(), Error> {
    let mut v: Value = json_or_json5_from_file(&file)?;

    // Extract includes if present.
    // For instance, if file is `{ "include": [ "foo", "bar" ], "baz": "qux" }`
    // then this will extract `[ "foo", "bar" ]`
    // and leave behind `{ "baz": "qux" }`.
    let includes: Vec<_> = v.as_object_mut().map_or(vec![], |v| {
        v.remove("include").map_or(vec![], |v| {
            v.as_array().map_or(vec![], |v| {
                v.iter().cloned().filter_map(|v| v.as_str().map(String::from)).collect()
            })
        })
    });

    // Merge contents of includes
    // TODO(shayba): also merge includes recursively
    for include in &includes {
        let path = includepath.join(include);
        let includev: Value = json_or_json5_from_file(&path).map_err(|e| {
            Error::parse(
                format!("Couldn't read include {}: {}", &path.display(), e),
                None,
                Some(&file),
            )
        })?;
        merge_json(&mut v, &includev)
            .map_err(|e| Error::internal(format!("Failed to merge with {}: {}", include, e)))?;
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
        if output.is_none() || includes.is_empty() {
            // A non-existent depfile is the same as an empty depfile
            if depfile_path.exists() {
                // Delete stale depfile
                fs::remove_file(depfile_path)?;
            }
        } else if let Some(output_path) = output {
            let depfile_contents = format!("{}:", output_path.display())
                + &includes
                    .iter()
                    .map(|i| format!(" {}", includepath.join(i).display()))
                    .collect::<String>()
                + "\n";
            fs::OpenOptions::new()
                .create(true)
                .truncate(true)
                .write(true)
                .open(depfile_path)?
                .write_all(depfile_contents.as_bytes())?;
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;
    use serde_json::json;
    use std::fmt::Display;
    use std::fs::File;
    use std::io::Read;
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
        merge_includes(
            cmx_path,
            Some(out_cmx_path.clone()),
            Some(cmx_depfile_path.clone()),
            tmp_dir.path().to_path_buf(),
        )
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
        let cml_path = tmp_file(
            &tmp_dir,
            "some.cml",
            "{include: [\"shard.cml\"], program: {binary: \"bin/hello_world\"}}",
        );
        tmp_file(&tmp_dir, "shard.cml", "{use: [{ protocol: [\"fuchsia.foo.Bar\"]}]}");

        let out_cml_path = tmp_dir.path().join("out.cml");
        let cml_depfile_path = tmp_dir.path().join("cml.d");
        merge_includes(
            cml_path,
            Some(out_cml_path.clone()),
            Some(cml_depfile_path.clone()),
            tmp_dir.path().to_path_buf(),
        )
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
        merge_includes(
            cmx_path,
            Some(out_cmx_path.clone()),
            Some(cmx_depfile_path.clone()),
            tmp_dir.path().to_path_buf(),
        )
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
        merge_includes(
            cmx_path,
            Some(out_cmx_path.clone()),
            Some(cmx_depfile_path.clone()),
            tmp_dir.path().to_path_buf(),
        )
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
        merge_includes(
            cmx_path,
            Some(out_cmx_path.clone()),
            Some(cmx_depfile_path.clone()),
            tmp_dir.path().to_path_buf(),
        )
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
        let result = merge_includes(
            cmx_path,
            Some(out_cmx_path.clone()),
            Some(cmx_depfile_path.clone()),
            tmp_dir.path().to_path_buf(),
        );

        assert_matches!(result, Err(Error::Parse { err, .. })
                        if err.starts_with("Couldn't read include ") && err.contains("doesnt_exist.cmx"));
    }
}
