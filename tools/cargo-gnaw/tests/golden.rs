// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file contains "golden" tests, which compare the output of known sample
//! `Cargo.toml` files with known fixed reference output files.

use {
    anyhow::Context,
    argh::FromArgs,
    // Without this, the test diffs are impractical to debug.
    pretty_assertions::assert_eq,
    std::fmt::{Debug, Display},
    std::path::{Path, PathBuf},
    tempfile,
};

#[derive(FromArgs, Debug)]
/// Paths to use in test. All paths are relative to where this test is executed.
///
/// These paths have to be relative when passed to this test on infra bots, so they are mapped
/// correctly, otherwise they won't be available at test runtime. It is safe to convert these to
/// absolute paths later in the test.
struct Paths {
    /// path to the directory where golden tests are placed.
    #[argh(option)]
    test_base_dir: String,
    /// path to `rustc` binary to use in test.
    #[argh(option)]
    rustc_binary_path: String,
    /// path to `gn` binary to use in test.
    #[argh(option)]
    gn_binary_path: String,
    /// path to `cargo` binary to use in test.
    #[argh(option)]
    cargo_binary_path: String,
    /// path to shared libraries directory to use in test.
    #[argh(option)]
    lib_path: String,
}

#[derive(PartialEq, Eq)]
struct DisplayAsDebug<T: Display>(T);

impl<T: Display> Debug for DisplayAsDebug<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        Display::fmt(&self.0, f)
    }
}

fn main() {
    let paths: Paths = argh::from_env();
    eprintln!("paths: {:?}", &paths);

    // Shared library setup for Linux and Mac.  Systems will ignore the settings
    // that don't apply to them.
    //
    // These values need to be absolute so they work regardless of the current working directory.
    std::env::set_var("LD_LIBRARY_PATH", Path::new(&paths.lib_path).canonicalize().unwrap());
    std::env::set_var("DYLD_LIBRARY_PATH", Path::new(&paths.lib_path).canonicalize().unwrap());

    // Cargo internally invokes rustc; but we must tell it to use the one from
    // our sandbox, and this is configured using the env variable "RUSTC".
    //
    // This value needs to be absolute so it works regardless of the current working directory.
    //
    // See:
    // https://doc.rust-lang.org/cargo/reference/environment-variables.html
    std::env::set_var("RUSTC", Path::new(&paths.rustc_binary_path).canonicalize().unwrap());

    #[derive(Debug)]
    struct TestCase {
        /// Manifest file path (`Cargo.toml`); relative to the base test directory.
        manifest_path: Vec<&'static str>,
        /// Expected file (`BUILD.gn`); relative to the base test directory.
        golden_expected_filename: Vec<&'static str>,
        /// If set, the flag `--skip-root` is added to `cargo_gnaw` invocation.
        skip_root: bool,
    }

    let tests = vec![
        TestCase {
            manifest_path: vec!["simple", "Cargo.toml"],
            golden_expected_filename: vec!["simple", "BUILD.gn"],
            skip_root: false,
        },
        TestCase {
            manifest_path: vec!["simple_deps", "Cargo.toml"],
            golden_expected_filename: vec!["simple_deps", "BUILD.gn"],
            skip_root: false,
        },
        TestCase {
            manifest_path: vec!["simple_deps", "Cargo.toml"],
            golden_expected_filename: vec!["simple_deps", "BUILD_WITH_NO_ROOT.gn"],
            skip_root: true,
        },
        TestCase {
            manifest_path: vec!["platform_deps", "Cargo.toml"],
            golden_expected_filename: vec!["platform_deps", "BUILD.gn"],
            skip_root: true,
        },
        TestCase {
            manifest_path: vec!["binary", "Cargo.toml"],
            golden_expected_filename: vec!["binary", "BUILD.gn"],
            skip_root: false,
        },
        TestCase {
            manifest_path: vec!["multiple_crate_types", "Cargo.toml"],
            golden_expected_filename: vec!["multiple_crate_types", "BUILD.gn"],
            skip_root: false,
        },
        TestCase {
            manifest_path: vec!["feature_review", "Cargo.toml"],
            golden_expected_filename: vec!["feature_review", "BUILD.gn"],
            skip_root: false,
        },
    ];

    let run_gnaw = |manifest_path: &[&str], skip_root| {
        let test_dir = tempfile::TempDir::new().unwrap();
        let mut manifest_path: PathBuf =
            test_dir.path().join(manifest_path.iter().collect::<PathBuf>());
        let output = test_dir.path().join("BUILD.gn");

        // we need the emitted file to be under the same path as the gn targets it references
        let test_base_dir = PathBuf::from(&paths.test_base_dir);
        copy_contents(&test_base_dir, test_dir.path());

        if manifest_path.file_name().unwrap() != "Cargo.toml" {
            // rename manifest so that `cargo metadata` is happy.
            let manifest_dest_path =
                manifest_path.parent().expect("getting Cargo.toml parent dir").join("Cargo.toml");
            std::fs::copy(&manifest_path, &manifest_dest_path).expect("writing Cargo.toml");
            manifest_path = manifest_dest_path;
        }

        let project_root = test_dir.path().to_str().unwrap().to_owned();
        // Note: argh does not support "--flag=value" or "--bool-flag false".
        let absolute_cargo_binary_path =
            Path::new(&paths.cargo_binary_path).canonicalize().unwrap();
        let mut args: Vec<&str> = vec![
            // args[0] is not used in arg parsing, so this can be any string.
            "fake_binary_name",
            "--manifest-path",
            manifest_path.to_str().unwrap(),
            "--project-root",
            &project_root,
            "--output",
            output.to_str().unwrap(),
            "--gn-bin",
            &paths.gn_binary_path,
            "--cargo",
            // Cargo is not executed in another working directory by gnaw_lib, so an absolute path
            // is necessary here.
            absolute_cargo_binary_path.to_str().unwrap(),
        ];
        if skip_root {
            args.push("--skip-root");
        }
        gnaw_lib::run(&args)
            .with_context(|| format!("error running gnaw with args: {:?}\n\t", &args))?;
        let output = std::fs::read_to_string(&output)
            .with_context(|| format!("while reading tempfile: {}", output.display()))
            .expect("tempfile read success");
        Result::<_, anyhow::Error>::Ok(output)
    };

    for test in tests {
        let output = run_gnaw(&test.manifest_path, test.skip_root)
            .with_context(|| format!("\n\ttest was: {:?}", &test))
            .expect("gnaw_lib::run should succeed");

        let test_base_dir = PathBuf::from(&paths.test_base_dir);
        let expected_path: PathBuf =
            test_base_dir.join(test.golden_expected_filename.iter().collect::<PathBuf>());
        let expected = std::fs::read_to_string(expected_path.to_string_lossy().to_string())
            .with_context(|| {
                format!("while reading expected: {:?}", &test.golden_expected_filename)
            })
            .expect("expected file read success");
        assert_eq!(
            DisplayAsDebug(expected),
            DisplayAsDebug(output),
            "left: expected; right: actual: {:?}",
            &test
        );
    }

    #[derive(Debug)]
    struct ExpectFailCase {
        /// Manifest file path (`Cargo.toml`); relative to the base test directory.
        manifest_path: Vec<&'static str>,
        /// Expected string to search for in returned error.
        expected_error_substring: &'static str,
        /// If set, the flag `--skip-root` is added to `cargo_gnaw` invocation.
        skip_root: bool,
    }
    let tests = vec![
        ExpectFailCase {
            manifest_path: vec!["feature_review", "Cargo_unreviewed_feature.toml"],
            expected_error_substring:
                "crate_with_features 0.1.0 is included with unreviewed features [\"feature1\"]",
            skip_root: false,
        },
        ExpectFailCase {
            manifest_path: vec!["feature_review", "Cargo_missing_review.toml"],
            expected_error_substring:
                "crate_with_features 0.1.0 requires feature review but reviewed features not found",
            skip_root: false,
        },
        ExpectFailCase {
            manifest_path: vec!["feature_review", "Cargo_extra_review.toml"],
            expected_error_substring:
                "crate_with_features 0.1.0 sets reviewed_features but crate_with_features was not found in require_feature_reviews",
            skip_root: false,
        },
    ];
    for test in tests {
        let result = run_gnaw(&test.manifest_path, test.skip_root);
        let error = match result {
            Ok(_) => panic!("gnaw unexpectedly succeeded for {:?}", test),
            Err(e) => e,
        };
        if error.chain().find(|e| e.to_string().contains(test.expected_error_substring)).is_none() {
            panic!(
                "expected error to contain {:?}, was: {:?}",
                test.expected_error_substring, error
            );
        }
    }
}

fn copy_contents(original_test_dir: &Path, test_dir_path: &Path) {
    // copy the contents of original test dir to test_dir
    for entry in walkdir::WalkDir::new(&original_test_dir) {
        let entry = entry.expect("walking original test directory to copy files to /tmp");
        if !entry.file_type().is_file() {
            continue;
        }
        let to_copy = entry.path();
        let destination = test_dir_path.join(to_copy.strip_prefix(&original_test_dir).unwrap());
        std::fs::create_dir_all(destination.parent().unwrap())
            .expect("making parent of file to copy");
        std::fs::copy(to_copy, destination).expect("copying file");
    }
    println!("done copying files");
}
