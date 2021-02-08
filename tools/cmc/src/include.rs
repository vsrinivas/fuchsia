// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error,
    crate::merge::merge_json,
    crate::util::{json_or_json5_from_file, write_depfile},
    serde_json::Value,
    std::{
        collections::HashSet,
        fs,
        io::{BufRead, BufReader, Write},
        iter::FromIterator,
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
    let includes = transitive_includes(&file, &includepath)?;
    let mut v: Value = json_or_json5_from_file(&file)?;
    v.as_object_mut().and_then(|v| v.remove("include"));

    for include in &includes {
        let path = includepath.join(&include);
        let mut includev: Value = json_or_json5_from_file(&path).map_err(|e| {
            Error::parse(
                format!("Couldn't read include {}: {}", &path.display(), e),
                None,
                Some(&file),
            )
        })?;
        includev.as_object_mut().and_then(|v| v.remove("include"));
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
        write_depfile(depfile_path, output, &includes, includepath)?;
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
    depfile: Option<&PathBuf>,
    stamp: Option<&PathBuf>,
    includepath: &PathBuf,
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
        if let Some(depfile_path) = depfile {
            if depfile_path.exists() {
                // Delete stale depfile
                fs::remove_file(depfile_path)?;
            }
        }
        return Ok(());
    }

    let actual = transitive_includes(&file, &includepath)?;
    for expected in expected_includes {
        if !actual.contains(&expected) {
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

    // Write includes to depfile
    if let Some(depfile_path) = depfile {
        write_depfile(depfile_path, stamp, &actual, includepath)?;
    }

    Ok(())
}

/// Returns all includes of a file relative to `includepath`.
/// Follows transitive includes.
/// Detects cycles.
/// Includes are returned in sorted order.
pub fn transitive_includes(file: &PathBuf, includepath: &PathBuf) -> Result<Vec<String>, Error> {
    fn helper(
        file: &PathBuf,
        includepath: &PathBuf,
        doc: &Value,
        entered: &mut HashSet<String>,
        exited: &mut HashSet<String>,
    ) -> Result<(), Error> {
        if let Some(includes) = doc.get("include").and_then(|v| v.as_array()) {
            for include in includes.into_iter().filter_map(|v| v.as_str().map(String::from)) {
                // Avoid visiting the same include more than once
                if !entered.insert(include.clone()) {
                    if !exited.contains(&include) {
                        return Err(Error::parse(
                            format!("Includes cycle at {}", include),
                            None,
                            Some(&file),
                        ));
                    }
                } else {
                    let path = includepath.join(&include);
                    let include_doc = json_or_json5_from_file(&path).map_err(|e| {
                        Error::parse(
                            format!("Couldn't read include {}: {}", &path.display(), e),
                            None,
                            Some(&file),
                        )
                    })?;
                    helper(&file, &includepath, &include_doc, entered, exited)?;
                    exited.insert(include);
                }
            }
        }
        Ok(())
    }

    let root = json_or_json5_from_file(&file)?;
    let mut entered = HashSet::new();
    let mut exited = HashSet::new();
    helper(&file, &includepath, &root, &mut entered, &mut exited)?;
    let mut includes = Vec::from_iter(exited);
    includes.sort();
    Ok(includes)
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

    struct TestContext {
        tmpdir_path: PathBuf,
        depfile: PathBuf,
        stamp: PathBuf,
        fromfile: PathBuf,
        output: PathBuf,
        _tmpdir: TempDir,
    }

    impl TestContext {
        fn new() -> Self {
            let tmpdir = TempDir::new().unwrap();
            let tmpdir_path = tmpdir.path();
            Self {
                depfile: tmpdir_path.join("depfile"),
                stamp: tmpdir_path.join("stamp"),
                fromfile: tmpdir_path.join("fromfile"),
                output: tmpdir_path.join("out"),
                tmpdir_path: tmpdir_path.to_path_buf(),
                _tmpdir: tmpdir,
            }
        }

        fn new_path(&self, name: &str) -> PathBuf {
            self.tmpdir_path.join(name)
        }

        fn new_file(&self, name: &str, contents: impl Display) -> PathBuf {
            let path = self.new_path(name);
            File::create(&path).unwrap().write_all(format!("{:#}", contents).as_bytes()).unwrap();
            path
        }

        fn merge_includes(&self, file: &PathBuf) -> Result<(), Error> {
            super::merge_includes(file, Some(&self.output), Some(&self.depfile), &self.tmpdir_path)
        }

        fn check_includes(
            &self,
            file: &PathBuf,
            expected_includes: Vec<String>,
        ) -> Result<(), Error> {
            let fromfile = if self.fromfile.exists() { Some(&self.fromfile) } else { None };
            super::check_includes(
                file,
                expected_includes,
                fromfile,
                Some(&self.depfile),
                Some(&self.stamp),
                &self.tmpdir_path,
            )
        }

        fn assert_output_eq(&self, contents: impl Display) {
            let mut actual = String::new();
            File::open(&self.output).unwrap().read_to_string(&mut actual).unwrap();
            assert_eq!(
                actual,
                format!("{:#}", contents),
                "Unexpected contents of {:?}",
                &self.output
            );
        }

        fn assert_depfile_eq(&self, out: &PathBuf, ins: &[&PathBuf]) {
            let mut actual = String::new();
            File::open(&self.depfile).unwrap().read_to_string(&mut actual).unwrap();
            let expected = format!(
                "{}:{}\n",
                out.display(),
                &ins.iter().map(|i| format!(" {}", i.display())).collect::<String>()
            );
            assert_eq!(actual, expected, "Unexpected contents of {:?}", &self.depfile);
        }

        fn assert_no_depfile(&self) {
            assert!(!self.depfile.exists());
        }
    }

    #[test]
    fn test_include_cmx() {
        let ctx = TestContext::new();
        let cmx_path = ctx.new_file(
            "some.cmx",
            json!({
                "include": ["shard.cmx"],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        let shard_path = ctx.new_file(
            "shard.cmx",
            json!({
                "sandbox": {
                    "services": ["fuchsia.foo.Bar"]
                }
            }),
        );
        ctx.merge_includes(&cmx_path).unwrap();

        ctx.assert_output_eq(json!({
            "program": {
                "binary": "bin/hello_world"
            },
            "sandbox": {
                "services": ["fuchsia.foo.Bar"]
            }
        }));
        ctx.assert_depfile_eq(&ctx.output, &[&shard_path]);
    }

    #[test]
    fn test_include_cml() {
        let ctx = TestContext::new();
        let cml_path = ctx.new_file(
            "some.cml",
            "{include: [\"shard.cml\"], program: {binary: \"bin/hello_world\"}}",
        );
        let shard_path = ctx.new_file("shard.cml", "{use: [{ protocol: [\"fuchsia.foo.Bar\"]}]}");
        ctx.merge_includes(&cml_path).unwrap();

        ctx.assert_output_eq(json!({
            "program": {
                "binary": "bin/hello_world"
            },
            "use": [{
                "protocol": ["fuchsia.foo.Bar"]
            }]
        }));
        ctx.assert_depfile_eq(&ctx.output, &[&shard_path]);
    }

    #[test]
    fn test_include_multiple_shards() {
        let ctx = TestContext::new();
        let cmx_path = ctx.new_file(
            "some.cmx",
            json!({
                "include": ["shard1.cmx", "shard2.cmx"],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        let shard1_path = ctx.new_file(
            "shard1.cmx",
            json!({
                "sandbox": {
                    "services": ["fuchsia.foo.Bar"]
                }
            }),
        );
        let shard2_path = ctx.new_file(
            "shard2.cmx",
            json!({
                "sandbox": {
                    "services": ["fuchsia.foo.Qux"]
                }
            }),
        );
        ctx.merge_includes(&cmx_path).unwrap();

        ctx.assert_output_eq(json!({
            "program": {
                "binary": "bin/hello_world"
            },
            "sandbox": {
                "services": ["fuchsia.foo.Bar", "fuchsia.foo.Qux"]
            }
        }));
        ctx.assert_depfile_eq(&ctx.output, &[&shard1_path, &shard2_path]);
    }

    #[test]
    fn test_include_recursively() {
        let ctx = TestContext::new();
        let cmx_path = ctx.new_file(
            "some.cmx",
            json!({
                "include": ["shard1.cmx"],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        let shard1_path = ctx.new_file(
            "shard1.cmx",
            json!({
                "include": ["shard2.cmx"],
                "sandbox": {
                    "services": ["fuchsia.foo.Bar"]
                }
            }),
        );
        let shard2_path = ctx.new_file(
            "shard2.cmx",
            json!({
                "sandbox": {
                    "services": ["fuchsia.foo.Qux"]
                }
            }),
        );
        ctx.merge_includes(&cmx_path).unwrap();

        ctx.assert_output_eq(json!({
            "program": {
                "binary": "bin/hello_world"
            },
            "sandbox": {
                "services": ["fuchsia.foo.Bar", "fuchsia.foo.Qux"]
            }
        }));
        ctx.assert_depfile_eq(&ctx.output, &[&shard1_path, &shard2_path]);
    }

    #[test]
    fn test_include_nothing() {
        let ctx = TestContext::new();
        let cmx_path = ctx.new_file(
            "some.cmx",
            json!({
                "include": [],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        ctx.merge_includes(&cmx_path).unwrap();

        ctx.assert_output_eq(json!({
            "program": {
                "binary": "bin/hello_world"
            }
        }));
        ctx.assert_no_depfile();
    }

    #[test]
    fn test_no_includes() {
        let ctx = TestContext::new();
        let cmx_path = ctx.new_file(
            "some.cmx",
            json!({
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        ctx.merge_includes(&cmx_path).unwrap();

        ctx.assert_output_eq(json!({
            "program": {
                "binary": "bin/hello_world"
            }
        }));
        ctx.assert_no_depfile();
    }

    #[test]
    fn test_invalid_include() {
        let ctx = TestContext::new();
        let cmx_path = ctx.new_file(
            "some.cmx",
            json!({
                "include": ["doesnt_exist.cmx"],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        let result = ctx.merge_includes(&cmx_path);

        assert_matches!(result, Err(Error::Parse { err, .. })
                        if err.starts_with("Couldn't read include ") && err.contains("doesnt_exist.cmx"));
    }

    #[test]
    fn test_include_detect_cycle() {
        let ctx = TestContext::new();
        let cmx_path = ctx.new_file(
            "some1.cmx",
            json!({
                "include": ["some2.cmx"],
            }),
        );
        ctx.new_file(
            "some2.cmx",
            json!({
                "include": ["some1.cmx"],
            }),
        );
        let result = ctx.merge_includes(&cmx_path);
        assert_matches!(result, Err(Error::Parse { err, .. }) if err.contains("Includes cycle"));
    }

    #[test]
    fn test_include_a_diamond_is_not_a_cycle() {
        // This is fine:
        //
        //   A
        //  / \
        // B   C
        //  \ /
        //   D
        let ctx = TestContext::new();
        let a_path = ctx.new_file(
            "a.cmx",
            json!({
                "include": ["b.cmx", "c.cmx"],
            }),
        );
        ctx.new_file(
            "b.cmx",
            json!({
                "include": ["d.cmx"],
            }),
        );
        ctx.new_file(
            "c.cmx",
            json!({
                "include": ["d.cmx"],
            }),
        );
        ctx.new_file("d.cmx", json!({}));
        let result = ctx.merge_includes(&a_path);
        assert_matches!(result, Ok(()));
    }

    #[test]
    fn test_expect_nothing() {
        let ctx = TestContext::new();
        let cmx1_path = ctx.new_file(
            "some1.cmx",
            json!({
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_matches!(ctx.check_includes(&cmx1_path, vec![]), Ok(()));
        // Don't generate depfile (or delete existing) if no includes found
        ctx.assert_no_depfile();

        let cmx2_path = ctx.new_file(
            "some2.cmx",
            json!({
                "include": [],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_matches!(ctx.check_includes(&cmx2_path, vec![]), Ok(()));

        let cmx3_path = ctx.new_file(
            "some3.cmx",
            json!({
                "include": [ "foo.cmx" ],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_matches!(ctx.check_includes(&cmx3_path, vec![]), Ok(()));
    }

    #[test]
    fn test_expect_something_present() {
        let ctx = TestContext::new();
        let cmx_path = ctx.new_file(
            "some.cmx",
            json!({
                "include": [ "foo.cmx", "bar.cmx" ],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        let foo_path = ctx.new_file("foo.cmx", json!({}));
        let bar_path = ctx.new_file("bar.cmx", json!({}));
        assert_matches!(ctx.check_includes(&cmx_path, vec!["bar.cmx".into()]), Ok(()));
        // Note that inputs are sorted to keep depfile contents stable,
        // so bar.cmx comes before foo.cmx.
        ctx.assert_depfile_eq(&ctx.stamp, &[&bar_path, &foo_path]);
    }

    #[test]
    fn test_expect_something_missing() {
        let ctx = TestContext::new();
        let cmx1_path = ctx.new_file(
            "some1.cmx",
            json!({
                "include": [ "foo.cmx", "bar.cmx" ],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        ctx.new_file("foo.cmx", json!({}));
        ctx.new_file("bar.cmx", json!({}));
        assert_matches!(ctx.check_includes(&cmx1_path, vec!["qux.cmx".into()]),
                        Err(Error::Validate { filename, .. }) if filename == cmx1_path.to_str().map(String::from));

        let cmx2_path = ctx.new_file(
            "some2.cmx",
            json!({
                // No includes
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_matches!(ctx.check_includes(&cmx2_path, vec!["qux.cmx".into()]),
                        Err(Error::Validate { filename, .. }) if filename == cmx2_path.to_str().map(String::from));
    }

    #[test]
    fn test_expect_something_transitive() {
        let ctx = TestContext::new();
        let cmx_path = ctx.new_file(
            "some.cmx",
            json!({
                "include": [ "foo.cmx" ],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        ctx.new_file("foo.cmx", json!({"include": [ "bar.cmx" ]}));
        ctx.new_file("bar.cmx", json!({}));
        assert_matches!(ctx.check_includes(&cmx_path, vec!["bar.cmx".into()]), Ok(()));
    }

    #[test]
    fn test_expect_fromfile() {
        let ctx = TestContext::new();
        let cmx_path = ctx.new_file(
            "some.cmx",
            json!({
                "include": [ "foo.cmx", "bar.cmx" ],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        ctx.new_file("foo.cmx", json!({}));
        ctx.new_file("bar.cmx", json!({}));

        let mut fromfile = LineWriter::new(File::create(ctx.fromfile.clone()).unwrap());
        writeln!(fromfile, "foo.cmx").unwrap();
        writeln!(fromfile, "bar.cmx").unwrap();
        assert_matches!(ctx.check_includes(&cmx_path, vec![]), Ok(()));

        // Add another include that's missing
        writeln!(fromfile, "qux.cmx").unwrap();
        assert_matches!(ctx.check_includes(&cmx_path, vec![]),
                        Err(Error::Validate { filename, .. }) if filename == cmx_path.to_str().map(String::from));
    }
}
