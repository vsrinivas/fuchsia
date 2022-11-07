// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error,
    crate::merge::merge_json,
    crate::util,
    crate::util::{json_or_json5_from_file, write_depfile},
    serde_json::Value,
    std::{
        collections::HashSet,
        ffi::OsStr,
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
    includepath: &Vec<PathBuf>,
    includeroot: &PathBuf,
) -> Result<(), Error> {
    let includes = transitive_includes(&file, includepath, &includeroot)?;
    let mut v: Value = json_or_json5_from_file(&file)?;
    v.as_object_mut().and_then(|v| v.remove("include"));

    for include in &includes {
        let mut includev: Value = json_or_json5_from_file(&include).map_err(|e| {
            Error::parse(format!("Couldn't read include {:?}: {}", &include, e), None, Some(&file))
        })?;
        includev.as_object_mut().and_then(|v| v.remove("include"));
        merge_json(&mut v, &includev).map_err(|e| {
            Error::parse(format!("Failed to merge with {:?}: {}", include, e), None, Some(&file))
        })?;
    }

    // Write postprocessed JSON
    if let Some(output_path) = output.as_ref() {
        util::ensure_directory_exists(&output_path)?;
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
        write_depfile(depfile_path, output, &includes)?;
    }

    Ok(())
}

const CHECK_INCLUDES_URL: &str = "https://fuchsia.dev/go/components/build-errors";

/// Read in the provided JSON file and ensure that it contains all expected includes.
pub fn check_includes(
    file: &PathBuf,
    mut expected_includes: Vec<String>,
    // If specified, this is a path to newline-delimited `expected_includes`
    fromfile: Option<&PathBuf>,
    depfile: Option<&PathBuf>,
    stamp: Option<&PathBuf>,
    includepath: &Vec<PathBuf>,
    includeroot: &PathBuf,
) -> Result<(), Error> {
    if file.extension() == Some(OsStr::new("cmx")) {
        // Don't worry about v1 manifests anymore.
        return Ok(());
    }

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

    let actual = transitive_includes(&file, includepath, &includeroot)?;
    for expected in
        expected_includes.iter().map(|i| canonicalize_include(&i, includepath, &includeroot))
    {
        if !actual.contains(&expected) {
            return Err(Error::Validate {
                schema_name: None,
                err: format!(
                    "{:?} must include {:?}.\nFor more details, see {}",
                    &file, &expected, CHECK_INCLUDES_URL
                ),
                filename: file.to_str().map(String::from),
            });
        }
    }

    // Write includes to depfile
    if let Some(depfile_path) = depfile {
        let mut inputs = actual.clone();
        inputs.push(file.clone());
        write_depfile(depfile_path, stamp, &inputs)?;
    }

    Ok(())
}

/// Returns all includes of a document.
/// Follows transitive includes.
/// Detects cycles.
/// Includes are returned in sorted order.
/// Includes are returned as canonicalized paths.
pub fn transitive_includes(
    file: &PathBuf,
    includepath: &Vec<PathBuf>,
    includeroot: &PathBuf,
) -> Result<Vec<PathBuf>, Error> {
    fn helper(
        includepath: &Vec<PathBuf>,
        includeroot: &PathBuf,
        doc: &Value,
        entered: &mut HashSet<PathBuf>,
        exited: &mut HashSet<PathBuf>,
    ) -> Result<(), Error> {
        if let Some(includes) = doc.get("include").and_then(|v| v.as_array()) {
            for include in includes
                .into_iter()
                .filter_map(|v| v.as_str().map(String::from))
                .map(|i| canonicalize_include(&i, includepath, &includeroot))
            {
                // Avoid visiting the same include more than once
                if !entered.insert(include.clone()) {
                    if !exited.contains(&include) {
                        return Err(Error::parse(
                            format!("Includes cycle at {:?}", include),
                            None,
                            None,
                        ));
                    }
                } else {
                    let include_doc = json_or_json5_from_file(&include).map_err(|e| {
                        Error::parse(
                            format!("Couldn't read include {:?}: {}", &include, e),
                            None,
                            None,
                        )
                    })?;
                    helper(includepath, &includeroot, &include_doc, entered, exited)?;
                    exited.insert(include);
                }
            }
        }
        Ok(())
    }

    let mut entered = HashSet::new();
    let mut exited = HashSet::new();
    let doc = json_or_json5_from_file(&file)?;
    helper(includepath, &includeroot, &doc, &mut entered, &mut exited)?;
    let mut includes = Vec::from_iter(exited);
    includes.sort();
    Ok(includes)
}

/// Resolves an include to a canonical path.
fn canonicalize_include(
    include: &String,
    includepath: &Vec<PathBuf>,
    includeroot: &PathBuf,
) -> PathBuf {
    if include.starts_with("//") {
        // Resolve against sources root
        return includeroot.join(&include[2..]);
    }
    for prefix in includepath {
        // Resolve against first matching includepath
        let resolved = prefix.join(&include);
        if resolved.exists() {
            return resolved;
        }
    }
    // Resolve against CWD
    return PathBuf::from(include);
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use serde_json::json;
    use std::fmt::Display;
    use std::fs::File;
    use std::io::{LineWriter, Read};
    use tempfile::TempDir;

    struct TestContext {
        // Root of source tree
        root_path: PathBuf,
        // Resolve includes in these paths
        include_path: Vec<PathBuf>,
        // Various inputs and outputs
        fromfile: PathBuf,
        depfile: PathBuf,
        stamp: PathBuf,
        output: PathBuf,
        _tmpdir: TempDir,
    }

    impl TestContext {
        fn new() -> Self {
            let tmpdir = TempDir::new().unwrap();
            let tmpdir_path = tmpdir.path();
            let includedir_path = tmpdir_path.join("includes");
            fs::create_dir(&includedir_path).unwrap();
            // Includes may also be resolved from a secondary directory
            let includedir_path_secondary = tmpdir_path.join("more_includes");
            fs::create_dir(&includedir_path_secondary).unwrap();
            let include_path = vec![includedir_path, includedir_path_secondary];
            Self {
                root_path: tmpdir_path.to_path_buf(),
                include_path: include_path,
                depfile: tmpdir_path.join("depfile"),
                stamp: tmpdir_path.join("stamp"),
                fromfile: tmpdir_path.join("fromfile"),
                output: tmpdir_path.join("out"),
                _tmpdir: tmpdir,
            }
        }

        fn new_include(&self, name: &str, contents: impl Display) -> PathBuf {
            let path = self.include_path[0].join(name);
            File::create(&path).unwrap().write_all(format!("{:#}", contents).as_bytes()).unwrap();
            path
        }

        fn new_include_secondary(&self, name: &str, contents: impl Display) -> PathBuf {
            let path = self.include_path[1].join(name);
            File::create(&path).unwrap().write_all(format!("{:#}", contents).as_bytes()).unwrap();
            path
        }

        fn new_file(&self, name: &str, contents: impl Display) -> PathBuf {
            let path = self.root_path.join(name);
            File::create(&path).unwrap().write_all(format!("{:#}", contents).as_bytes()).unwrap();
            return path;
        }

        fn new_dir(&self, name: &str) {
            fs::create_dir_all(self.root_path.join(name)).unwrap();
        }

        fn merge_includes(&self, file: &PathBuf) -> Result<(), Error> {
            super::merge_includes(
                file,
                Some(&self.output),
                Some(&self.depfile),
                &self.include_path,
                &self.root_path,
            )
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
                &self.include_path,
                &self.root_path,
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
        let shard_path = ctx.new_include(
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
        let shard_path =
            ctx.new_include("shard.cml", "{use: [{ protocol: [\"fuchsia.foo.Bar\"]}]}");
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
    fn test_include_not_from_first_path() {
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
        let shard_path = ctx.new_include_secondary(
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
    fn test_include_paths_take_priority_by_order() {
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
        let shard_path = ctx.new_include(
            "shard.cmx",
            json!({
                "sandbox": {
                    "services": ["fuchsia.foo.Bar"]
                }
            }),
        );
        // This shard won't be included because the one above will match first
        ctx.new_include_secondary(
            "shard.cmx",
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
                "services": ["fuchsia.foo.Bar"]
            }
        }));
        ctx.assert_depfile_eq(&ctx.output, &[&shard_path]);
    }

    #[test]
    fn test_include_absolute() {
        let ctx = TestContext::new();
        ctx.new_dir("path/to");
        let cmx_path = ctx.new_include(
            "some.cmx",
            json!({
                "include": ["//path/to/shard.cmx"],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        let shard_path = ctx.new_file(
            "path/to/shard.cmx",
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
    fn test_include_multiple_shards() {
        let ctx = TestContext::new();
        let cmx_path = ctx.new_include(
            "some.cmx",
            json!({
                "include": ["shard1.cmx", "shard2.cmx"],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        let shard1_path = ctx.new_include(
            "shard1.cmx",
            json!({
                "sandbox": {
                    "services": ["fuchsia.foo.Bar"]
                }
            }),
        );
        let shard2_path = ctx.new_include(
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
        let cmx_path = ctx.new_include(
            "some.cmx",
            json!({
                "include": ["shard1.cmx"],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        let shard1_path = ctx.new_include(
            "shard1.cmx",
            json!({
                "include": ["shard2.cmx"],
                "sandbox": {
                    "services": ["fuchsia.foo.Bar"]
                }
            }),
        );
        let shard2_path = ctx.new_include(
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
        let cmx_path = ctx.new_include(
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
        ctx.assert_depfile_eq(&ctx.output, &[]);
    }

    #[test]
    fn test_no_includes() {
        let ctx = TestContext::new();
        let cmx_path = ctx.new_include(
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
        ctx.assert_depfile_eq(&ctx.output, &[]);
    }

    #[test]
    fn test_invalid_include() {
        let ctx = TestContext::new();
        let cmx_path = ctx.new_include(
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
        let cmx_path = ctx.new_include(
            "some1.cmx",
            json!({
                "include": ["some2.cmx"],
            }),
        );
        ctx.new_include(
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
        let a_path = ctx.new_include(
            "a.cmx",
            json!({
                "include": ["b.cmx", "c.cmx"],
            }),
        );
        ctx.new_include(
            "b.cmx",
            json!({
                "include": ["d.cmx"],
            }),
        );
        ctx.new_include(
            "c.cmx",
            json!({
                "include": ["d.cmx"],
            }),
        );
        ctx.new_include("d.cmx", json!({}));
        let result = ctx.merge_includes(&a_path);
        assert_matches!(result, Ok(()));
    }

    #[test]
    fn test_expect_nothing() {
        let ctx = TestContext::new();
        let cmx1_path = ctx.new_include(
            "some1.cml",
            json!({
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_matches!(ctx.check_includes(&cmx1_path, vec![]), Ok(()));
        // Don't generate depfile (or delete existing) if no includes found
        ctx.assert_no_depfile();

        let cmx2_path = ctx.new_include(
            "some2.cml",
            json!({
                "include": [],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_matches!(ctx.check_includes(&cmx2_path, vec![]), Ok(()));

        let cmx3_path = ctx.new_include(
            "some3.cml",
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
        let cml_path = ctx.new_include(
            "some.cml",
            json!({
                "include": [ "foo.cml", "bar.cml" ],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        let foo_path = ctx.new_include("foo.cml", json!({}));
        let bar_path = ctx.new_include("bar.cml", json!({}));
        assert_matches!(ctx.check_includes(&cml_path, vec!["bar.cml".into()]), Ok(()));
        // Note that inputs are sorted to keep depfile contents stable,
        // so bar.cmx comes before foo.cmx.
        ctx.assert_depfile_eq(&ctx.stamp, &[&bar_path, &foo_path, &cml_path]);
    }

    #[test]
    fn test_expect_something_missing() {
        let ctx = TestContext::new();
        let cml1_path = ctx.new_include(
            "some1.cml",
            json!({
                "include": [ "foo.cml", "bar.cml" ],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        ctx.new_include("foo.cml", json!({}));
        ctx.new_include("bar.cml", json!({}));
        assert_matches!(ctx.check_includes(&cml1_path, vec!["qux.cml".into()]),
                        Err(Error::Validate { filename, .. }) if filename == cml1_path.to_str().map(String::from));

        let cml2_path = ctx.new_include(
            "some2.cml",
            json!({
                // No includes
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        assert_matches!(ctx.check_includes(&cml2_path, vec!["qux.cml".into()]),
                        Err(Error::Validate { filename, .. }) if filename == cml2_path.to_str().map(String::from));
    }

    #[test]
    fn test_expect_something_transitive() {
        let ctx = TestContext::new();
        let cml_path = ctx.new_include(
            "some.cml",
            json!({
                "include": [ "foo.cml" ],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        ctx.new_include("foo.cml", json!({"include": [ "bar.cml" ]}));
        ctx.new_include("bar.cml", json!({}));
        assert_matches!(ctx.check_includes(&cml_path, vec!["bar.cml".into()]), Ok(()));
    }

    #[test]
    fn test_expect_fromfile() {
        let ctx = TestContext::new();
        let cml_path = ctx.new_include(
            "some.cml",
            json!({
                "include": [ "foo.cml", "bar.cml" ],
                "program": {
                    "binary": "bin/hello_world"
                }
            }),
        );
        ctx.new_include("foo.cml", json!({}));
        ctx.new_include("bar.cml", json!({}));

        let mut fromfile = LineWriter::new(File::create(ctx.fromfile.clone()).unwrap());
        writeln!(fromfile, "foo.cml").unwrap();
        writeln!(fromfile, "bar.cml").unwrap();
        assert_matches!(ctx.check_includes(&cml_path, vec![]), Ok(()));

        // Add another include that's missing
        writeln!(fromfile, "qux.cml").unwrap();
        assert_matches!(ctx.check_includes(&cml_path, vec![]),
                        Err(Error::Validate { filename, .. }) if filename == cml_path.to_str().map(String::from));
    }
}
