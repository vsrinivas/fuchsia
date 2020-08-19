// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, format_err, Error},
    std::path::Path,
    std::process::{Command, Output},
};

enum Input {
    Snapshot(String),
}

/// Returns the path relative to the current exectuable.
#[cfg(not(target_os = "fuchsia"))]
fn path_for_file(filename: &str, relative_to: Option<&Path>) -> Result<String, Error> {
    use std::env;

    let mut path = env::current_exe().unwrap();
    let search_path = relative_to.unwrap_or(Path::new(".")).join(filename);

    // We don't know exactly where the binary is in the out directory (varies by target platform and
    // architecture), so search up the file tree for the given file.
    loop {
        if path.join(&search_path).exists() {
            path.push(search_path.clone());
            break Ok(path.to_str().unwrap().to_string());
        }
        if !path.pop() {
            // Reached the root of the file system
            break Err(format_err!(
                "Couldn't find {:?} near {:?}",
                search_path,
                env::current_exe().unwrap()
            ));
        }
    }
}

/// This is needed to work correctly in CQ. If this call fails make sure
/// that you have added the file to the `copy` target in the BUILD.gn file.
fn config_file_path(filename: &str) -> Result<String, Error> {
    path_for_file(filename, Some(&Path::new("test_data").join("triage").join("config")))
}

/// This is needed to work correctly in CQ. If this call fails make sure
/// that you have added the file to the `copy` target in the BUILD.gn file.
fn snapshot_path() -> Result<String, Error> {
    path_for_file("snapshot", Some(&Path::new("test_data").join("triage")))
}

/// This is needed to work correctly in CQ. If this call fails make sure
/// that you have added the file to the `copy` target in the BUILD.gn file.
fn inspect_file_path() -> Result<String, Error> {
    path_for_file("inspect.json", Some(&Path::new("test_data").join("triage").join("snapshot")))
}

/// This is needed to work correctly in CQ. If this call fails make sure
/// that you have added the file to the `copy` target in the BUILD.gn file.
fn annotations_file_path() -> Result<String, Error> {
    path_for_file("annotations.json", Some(&Path::new("test_data").join("triage").join("snapshot")))
}

/// Returns the path to the triage binary.
fn binary_path() -> Result<String, Error> {
    path_for_file("triage", None)
}

/// Executes the command with the given arguments
fn run_command(
    input: Input,
    configs: Vec<String>,
    tags: Vec<String>,
    exclude_tags: Vec<String>,
) -> Result<Output, Error> {
    let mut args = Vec::new();

    match input {
        Input::Snapshot(snapshot) => {
            args.push("--data".to_string());
            args.push(snapshot);
        }
    }

    for config in configs {
        args.push("--config".to_string());
        args.push(config);
    }

    for tag in tags {
        args.push("--tag".to_string());
        args.push(tag);
    }

    for tag in exclude_tags {
        args.push("--exclude-tag".to_string());
        args.push(tag);
    }

    match Command::new(binary_path()?).args(args).output() {
        Ok(o) => Ok(o),
        Err(err) => Err(anyhow!("Command didn't run: {:?}", err.kind())),
    }
}

enum StringMatch {
    Contains(&'static str),
    DoesNotContain(&'static str),
}

fn verify_output(output: Output, status_code: i32, expected_text: StringMatch) {
    // validate the status code
    let output_status_code = output.status.code().expect("unable to unwrap status code");
    assert_eq!(
        output_status_code, status_code,
        "unexpected status code: got {}, expected {}",
        output_status_code, status_code
    );

    // validate the output text
    let stdout = std::str::from_utf8(&output.stdout).expect("Non-UTF8 return from command");
    match expected_text {
        StringMatch::Contains(s) => {
            assert_eq!(stdout.contains(s), true, "{} does not contain: {}", stdout, s)
        }
        StringMatch::DoesNotContain(s) => {
            assert_eq!(stdout.contains(s), false, "{} should not contain: {}", stdout, s)
        }
    };
}

#[test]
fn config_file_path_should_find_file() {
    assert!(config_file_path("sample.triage").is_ok(), "should be able to find sample.triage file");
}

#[test]
fn snapshot_path_should_find_snapshot() {
    assert!(snapshot_path().is_ok(), "should be able to find the snapshot path");
}

#[test]
fn inspect_file_path_should_find_file() {
    assert!(inspect_file_path().is_ok(), "should be able to find the inspect.json file");
}

#[test]
fn annotations_file_path_should_find_file() {
    assert!(annotations_file_path().is_ok(), "should be able to find the annotations.json file");
}

#[test]
fn binary_path_should_find_binary() {
    assert!(binary_path().is_ok(), "should be able to find the triage binary");
}

/// Macro to easily add an integration test to the target
///
/// The file paths can be named files that are incldued in the copy phase of the
/// build. The paths will be expanded to correctly work in CQ
/// ```
/// integration_test!(
///     my_test,            // The name of the test
///     vec!["foo.triage"], // a list of config files
///     vec![],             // any tags to include
///     vec![],             // any tags to exclude
///     0,                  // The expected status code
///     "some text"        // A substring to search for (alternatively call 'not "some text") to exclude the text
/// );
/// ```
macro_rules! integration_test {
    (@internal $name:ident, $config:expr, $tags:expr, $exclude_tags:expr,
        $status_code:expr, $string_match:expr) => {

        #[test]
        fn $name() -> Result<(), Error> {
            let output = crate::test::integration::run_command(
                Input::Snapshot(crate::test::integration::snapshot_path()?),
                $config
                    .into_iter()
                    .map(|c| crate::test::integration::config_file_path(c).unwrap())
                    .collect(),
                $tags.into_iter().map(|t: &str| t.to_string()).collect(),
                $exclude_tags.into_iter().map(|t: &str| t.to_string()).collect(),
            )?;
            crate::test::integration::verify_output(output, $status_code, $string_match);
            Ok(())
        }
    };
    ($name:ident, $config:expr, $tags:expr, $exclude_tags:expr,
        $status_code:expr, not $substring:expr
    ) => {
        integration_test!(@internal $name, $config, $tags,
            $exclude_tags ,$status_code, StringMatch::DoesNotContain($substring));
    };
    ($name:ident, $config:expr, $tags:expr, $exclude_tags:expr,
        $status_code:expr, $substring:expr) => {
        integration_test!(@internal $name, $config, $tags,
            $exclude_tags, $status_code, StringMatch::Contains($substring));
    };
}

#[test]
fn report_missing_inspect() -> Result<(), Error> {
    //note: we do not use the macro here because we want to not fail on the
    // file conversion logic
    let output = run_command(
        Input::Snapshot("not_found_dir".to_string()),
        vec![config_file_path("sample.triage")?],
        vec![],
        vec![],
    )?;
    verify_output(
        output,
        1,
        StringMatch::Contains("Couldn't read file 'not_found_dir/inspect.json'"),
    );
    Ok(())
}

#[test]
fn report_missing_config_file() -> Result<(), Error> {
    //note: we do not use the macro here because we want to not fail on the
    // file conversion logic
    let output =
        run_command(Input::Snapshot(snapshot_path()?), vec!["cfg".to_string()], vec![], vec![])?;
    verify_output(output, 1, StringMatch::Contains("Couldn't read config file"));
    Ok(())
}

integration_test!(
    successfully_read_correct_files,
    vec!["other.triage", "sample.triage"],
    vec![],
    vec![],
    1,
    not "Couldn't"
);

integration_test!(
    use_namespace_in_actions,
    vec!["other.triage", "sample.triage"],
    vec![],
    vec![],
    1,
    "[WARNING] yes on A!"
);

integration_test!(
    use_namespace_in_metrics,
    vec!["other.triage", "sample.triage"],
    vec![],
    vec![],
    1,
    "[WARNING] Used some of disk"
);

integration_test!(
    fail_on_missing_namespace,
    vec!["sample.triage"],
    vec![],
    vec![],
    1,
    "Bad namespace"
);

integration_test!(
    include_tagged_actions,
    vec!["sample_tags.triage"],
    vec!["foo"],
    vec![],
    1,
    "[WARNING] trigger foo tag"
);

integration_test!(
    only_runs_included_actions,
    vec!["sample_tags.triage"],
    vec!["not_included"],
    vec![],
    0,
    ""
);

integration_test!(
    included_tags_override_excludes,
    vec!["sample_tags.triage"],
    vec!["foo"],
    vec!["foo"],
    1,
    "[WARNING] trigger foo tag"
);

integration_test!(
    exclude_actions_with_excluded_tags,
    vec!["sample_tags.triage"],
    vec![],
    vec!["foo"],
    0,
    ""
);

integration_test!(
    error_rate_with_moniker_payload,
    vec!["error_rate.triage"],
    vec![],
    vec![],
    1,
    "[WARNING] Error rate for app.cmx is too high"
);

integration_test!(
    annotation_test,
    vec!["annotation_tests.triage"],
    vec![],
    vec![],
    1,
    "[WARNING] Running on a chromebook"
);

integration_test!(
    annotation_test2,
    vec!["annotation_tests.triage"],
    vec![],
    vec![],
    1,
    not "[WARNING] Not using a chromebook"
);

integration_test!(
    map_fold_test,
    vec!["map_fold.triage"],
    vec![],
    vec![],
    1,
    "Everything worked as expected"
);

integration_test!(log_tests, vec!["log_tests.triage"], vec![], vec![], 0, "");
