// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ser::{Expectation, Include, UnmergedExpectation};

#[derive(FromArgs)]
/// Merge and validate expectations files
struct PreprocessArgs {
    /// root expectations file (relative to current working directory)
    #[argh(option)]
    root_expectations_file: std::path::PathBuf,

    /// location to write depfile
    #[argh(option)]
    depfile: std::path::PathBuf,

    /// location to write merged expectations file
    #[argh(option)]
    preprocessed_expectations_file: std::path::PathBuf,
}

fn main() {
    preprocess(
        // When this is invoked as a GN compiled_action, the current working
        // directory will be $root_build_dir in the GN sense
        // (see `fx gn help root_build_dir`).
        std::path::Path::new(""),
        argh::from_env(),
    )
}

fn preprocess(
    root_build_dir: &std::path::Path,
    PreprocessArgs {
        root_expectations_file,
        depfile,
        preprocessed_expectations_file,
    }: PreprocessArgs,
) {
    let mut discovered_deps = std::collections::HashSet::new();
    let mut ancestors = std::collections::HashSet::from([root_expectations_file.clone()]);
    let merged_expectations = ser::Expectations {
        expectations: {
            let mut expectations = Vec::new();
            push_expectations(
                root_build_dir,
                &mut expectations,
                &mut discovered_deps,
                &mut ancestors,
                &root_build_dir.join(root_expectations_file),
            );
            expectations
        },
    };
    let discovered_deps = discovered_deps;

    std::fs::write(
        &preprocessed_expectations_file,
        serde_json5::to_string(&merged_expectations)
            .expect("failed to serialize merged expectations"),
    )
    .expect("failed to write merged expectations file");

    // Because we are constructing the dependencies of this expectations file at
    // preprocess-time (the dependencies are encoded as "include"s within each
    // expectations file rather than in GN directly), we must write a depfile
    // so that GN can track our dependencies.
    // See `fx gn help depfile` for more information.
    std::fs::write(
        &depfile,
        format!(
            "{}: {}",
            preprocessed_expectations_file.display(),
            discovered_deps
                .into_iter()
                .map(|path| path.display().to_string())
                .collect::<Vec<String>>()
                .join(" "),
        ),
    )
    .expect("failed to write depfile");
}

fn push_expectations(
    root_build_dir: &std::path::Path,
    expectations: &mut Vec<Expectation>,
    discovered_deps: &mut std::collections::HashSet<std::path::PathBuf>,
    ancestors: &mut std::collections::HashSet<std::path::PathBuf>,
    curr_path: &std::path::Path,
) {
    let ser::UnmergedExpectations { expectations: unmerged_expectations } =
        serde_json5::from_str(&std::fs::read_to_string(curr_path).unwrap_or_else(|err| {
            panic!("failed to read expectations file from {}: {err}", curr_path.display())
        }))
        .unwrap_or_else(|err| {
            panic!("failed to parse expectations file from {}: {err}", curr_path.display())
        });
    for unmerged_expectation in unmerged_expectations {
        match unmerged_expectation {
            UnmergedExpectation::Expectation(expectation) => expectations.push(expectation),
            UnmergedExpectation::Include(Include { path }) => {
                let mut ancestors = ancestors.clone();
                let expectations_file_path = {
                    if let Some(fuchsia_dir_relative_path) = path.strip_prefix("//") {
                        // Got absolute path (or rather, path relative to $FUCHSIA_DIR)
                        std::path::PathBuf::from_iter([
                            root_build_dir,
                            std::path::Path::new("../.."),
                            std::path::Path::new(fuchsia_dir_relative_path),
                        ])
                    } else {
                        // Got path relative to current file's parent
                        curr_path.parent().unwrap().join(path)
                    }
                };
                let _newly_inserted: bool = discovered_deps.insert(expectations_file_path.clone());
                assert!(
                    /* newly_inserted= */ ancestors.insert(expectations_file_path.clone()),
                    "detected a dependency cycle including {}",
                    &expectations_file_path.display()
                );
                push_expectations(
                    root_build_dir,
                    expectations,
                    discovered_deps,
                    &mut ancestors,
                    &expectations_file_path,
                );
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::{preprocess, PreprocessArgs};
    use std::str::FromStr as _;

    struct TestFile {
        path: std::path::PathBuf,
        contents: String,
    }

    fn test_preprocessor(
        root_file: &TestFile,
        other_files: &[TestFile],
        expected_preprocessed_file: serde_json::Value,
    ) {
        let temp_dir = tempfile::TempDir::new().unwrap();
        let fuchsia_dir = temp_dir.path();
        let root_build_dir = fuchsia_dir.join(std::path::Path::new("out/not-not-default"));
        std::fs::create_dir_all(&root_build_dir).unwrap();

        for TestFile { path, contents } in std::iter::once(root_file).chain(other_files) {
            let path = fuchsia_dir.join(path);
            std::fs::create_dir_all(path.parent().unwrap()).unwrap();
            std::fs::write(&path, contents).unwrap();
        }

        let actual_filepath =
            root_build_dir.join(std::path::Path::new("actual_preprocessed_file.json"));

        preprocess(
            &root_build_dir,
            PreprocessArgs {
                root_expectations_file: pathdiff::diff_paths(
                    fuchsia_dir.join(&root_file.path),
                    root_build_dir.clone(),
                )
                .unwrap(),
                depfile: root_build_dir.join(std::path::Path::new("testdepfile.d")),
                preprocessed_expectations_file: actual_filepath.clone(),
            },
        );

        let actual_contents = std::fs::read_to_string(&actual_filepath).unwrap();

        pretty_assertions::assert_eq!(
            serde_json::Value::from_str(&actual_contents).unwrap(),
            expected_preprocessed_file
        );
    }

    #[test]
    fn noop() {
        const CONTENTS: &'static str = r#"
// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
{
    actions: [
        {
            type: "expect_failure",
            matchers: [ "some_case_expected_to_fail" ],
        },
        {
            type: "expect_pass",
            matchers: [ "some_cases_*_expected_to_pass/*" ],
        },
        // Inline comments too
        {
            type: "skip",
            matchers: [
                "*skip_these_cases*",
            ],
        },
    ],
}
"#;
        test_preprocessor(
            &TestFile { path: "root.json5".into(), contents: CONTENTS.to_string() },
            &[],
            serde_json5::from_str(CONTENTS).unwrap(),
        )
    }

    #[test]
    fn include() {
        const ROOT_CONTENTS: &str = r#"
{
    actions: [
        {
            include: "some/relative/path.json5",
        },
        {
            include: "//an/absolute/path.json5",
        },
    ],
}
"#;
        const RELATIVE_CONTENTS: &str = r#"
{
    actions: [
        {
            type: "expect_failure",
            matchers: [ "relative_case" ],
        },
    ],
}
"#;
        const ABSOLUTE_CONTENTS: &str = r#"
{
    actions: [
        {
            type: "expect_failure",
            matchers: [ "absolute_case" ],
        },
        {
            // Including the same file multiple times is allowed.
            include: "//place/in/tree/some/relative/path.json5",
        },
    ],
}
"#;

        test_preprocessor(
            &TestFile {
                path: "place/in/tree/root.json5".into(),
                contents: ROOT_CONTENTS.to_string(),
            },
            &[
                TestFile {
                    path: "place/in/tree/some/relative/path.json5".into(),
                    contents: RELATIVE_CONTENTS.to_string(),
                },
                TestFile {
                    path: "an/absolute/path.json5".into(),
                    contents: ABSOLUTE_CONTENTS.to_string(),
                },
            ],
            serde_json::json!({
                "actions": [
                    {
                        "type": "expect_failure",
                        "matchers": [ "relative_case" ],
                    },
                    {
                        "type": "expect_failure",
                        "matchers": [ "absolute_case" ],
                    },
                    {
                        "type": "expect_failure",
                        "matchers": [ "relative_case" ],
                    },
                ],
            }),
        )
    }

    #[test]
    #[should_panic]
    fn dependency_cycle() {
        const ROOT_CONTENTS: &str = r#"
{
    actions: [
        {
            include: "a.json5",
        },
    ],
}
"#;
        const A_CONTENTS: &str = r#"
{
    actions: [
        {
            include: "b.json5",
        },
    ],
}
"#;

        const B_CONTENTS: &str = r#"
{
    actions: [
        {
            include: "a.json5",
        },
    ],
}
"#;

        test_preprocessor(
            &TestFile { path: "root.json5".into(), contents: ROOT_CONTENTS.to_string() },
            &[
                TestFile { path: "a.json5".into(), contents: A_CONTENTS.to_string() },
                TestFile { path: "b.json5".into(), contents: B_CONTENTS.to_string() },
            ],
            serde_json::json!("unused"),
        )
    }
}
