// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file contains "golden" tests, which compare the output of known sample
//! `Cargo.toml` files with known fixed reference output files.

use {
    anyhow::Context,
    // Without this, the test diffs are impractical to debug.
    pretty_assertions::assert_eq,
    std::path::PathBuf,
    tempfile,
};

/// All the paths to runfiles and tools which are used in this test.
///
/// All paths are absolute, and are resolved based on knowing that they are all
/// beneath the directory in which this test binary is stored.  See the `BUILD.gn`
/// file for this test target and the corresponding `host_test_data` targets.
///
/// Note that it is not possible to refer to paths inside the source tree, because
/// the source infra runners only have access to the output artifacts (i.e. contents
/// of the "out" directory).
#[derive(Debug)]
struct Paths {
    /// `.../host_x64`
    test_root_dir: PathBuf,

    /// `.../host_x64/test_data`, this is the root of the runfilfes tree, a
    /// path //foo/bar will be copied at `.../host_x64/test_data/foo/bar` for
    /// this test.
    test_data_dir: PathBuf,

    /// `.../host_x64/test_data/tools/cargo-gnaw/tests`: this is the directory
    /// where golden tests are placed. Corresponds to `//tools/cargo-gnaw/tests`.
    test_base_dir: PathBuf,

    /// `.../host_x64/test_data/tools/cargo-gnaw/runfiles`: this is the directory
    /// where the binary runfiles live.
    runfiles_dir: PathBuf,

    /// `.../runfiles/rust/bin/rustc`: the path to the rustc binary.  rustc is
    /// used by cargo.
    rustc_binary_path: PathBuf,

    /// `.../runfiles/gn`: the absolute path to the gn binary. gn is used for
    /// formatting.
    gn_binary_path: PathBuf,

    /// `.../runfiles/gn`: the absolute path to the cargo binary.
    cargo_binary_path: PathBuf,

    /// `.../runfiles/rust/lib`: the absolute path to the directory where the
    /// shared libraries are stored.
    lib_path: PathBuf,
}

/// Gets the hermetic test paths for the runfiles and tools used in this test.
///
/// The hermetic test paths are computed based on the parent directory of this
/// binary.
fn get_paths() -> Paths {
    let args: Vec<String> = std::env::args().collect();
    eprintln!("test args: {:?}", &args);

    let test_binary_path =
        std::fs::canonicalize([&args[0]].iter().collect::<PathBuf>()).expect("existing path");

    let test_root_dir = test_binary_path.parent().unwrap();

    let test_data_dir: PathBuf = [test_root_dir.to_str().unwrap(), "test_data"].iter().collect();

    let test_base_dir: PathBuf =
        [test_data_dir.to_str().unwrap(), "tools", "cargo-gnaw", "tests"].iter().collect();

    let runfiles_dir: PathBuf =
        [test_root_dir.to_str().unwrap(), "test_data", "tools", "cargo-gnaw", "runfiles"]
            .iter()
            .collect();

    // Cargo needs rustc under the hoods, provide it.
    let rustc_binary_path: PathBuf =
        [runfiles_dir.to_str().unwrap(), "rust", "bin", "rustc"].iter().collect();

    let gn_binary_path: PathBuf = [runfiles_dir.to_str().unwrap(), "gn", "gn"].iter().collect();

    let cargo_binary_path: PathBuf =
        [runfiles_dir.to_str().unwrap(), "rust", "bin", "cargo"].iter().collect();

    // Set the shared library path to use for loading libraries that rustc needs.
    let lib_path: PathBuf = [runfiles_dir.to_str().unwrap(), "rust", "lib"].iter().collect();
    eprintln!("lib_path: {:?}", &lib_path);

    Paths {
        test_root_dir: test_root_dir.to_path_buf(),
        test_data_dir,
        test_base_dir,
        runfiles_dir,
        rustc_binary_path,
        gn_binary_path,
        cargo_binary_path,
        lib_path,
    }
}

#[test]
fn build_file_generation_test() {
    let paths = get_paths();
    eprintln!("paths: {:?}", &paths);

    // Shared library setup for Linux and Mac.  Systems will ignore the settings
    // that don't apply to them.
    std::env::set_var("LD_LIBRARY_PATH", &paths.lib_path);
    std::env::set_var("DYLD_LIBRARY_PATH", &paths.lib_path);

    // Cargo internally invokes rustc; but we must tell it to use the one from
    // our sandbox, and this is configured using the env variable "RUSTC".
    // See:
    // https://doc.rust-lang.org/cargo/reference/environment-variables.html
    std::env::set_var("RUSTC", &paths.rustc_binary_path);

    // Test are executing from the test base directory.
    std::env::set_current_dir(&paths.test_base_dir).unwrap();
    eprintln!("current dir: {:?}", std::env::current_dir());

    #[derive(Debug)]
    struct TestCase {
        /// Manifest file path (`Cargo.toml`); relative to the base test directory.
        manifest_path: Vec<&'static str>,
        /// Expected file (`BUILD.gn`); relative to the base test directory.
        golden_expected_filename: Vec<&'static str>,
        /// If set, the flag `--skip-root` is added to `cargo_gnaw` invocation.
        skip_root: bool,
    };

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
    ];

    for test in tests {
        let manifest_path: PathBuf =
            std::fs::canonicalize(test.manifest_path.iter().collect::<PathBuf>()).unwrap();
        let output = tempfile::NamedTempFile::new().expect("could not open a tempfile");

        // Note: argh does not support "--flag=value" or "--bool-flag false".
        let mut args: Vec<&str> = vec![
            // args[0] is not used in arg parsing, so this can be any string.
            "fake_binary_name",
            "--manifest-path",
            manifest_path.to_str().unwrap(),
            "--project-root",
            paths.test_data_dir.to_str().unwrap(),
            "--output",
            output.path().to_str().unwrap(),
            "--gn-bin",
            paths.gn_binary_path.to_str().unwrap(),
            "--cargo",
            paths.cargo_binary_path.to_str().unwrap(),
        ];
        if test.skip_root {
            args.push("--skip-root");
        }
        gnaw_lib::run(&args)
            .with_context(|| format!("\n\targs were: {:?}\n\ttest was: {:?}", &args, &test))
            .expect("gnaw_lib::run should succeed");
        let output = std::fs::read_to_string(output.path())
            .with_context(|| format!("while reading tempfile: {:?}", output.path()))
            .expect("tempfile read success");

        let expected_path: PathBuf = test.golden_expected_filename.iter().collect();
        let expected = std::fs::read_to_string(expected_path.to_string_lossy().to_string())
            .with_context(|| {
                format!("while reading expected: {:?}", &test.golden_expected_filename)
            })
            .expect("expected file read success");
        assert_eq!(expected, output, "left: expected; right: actual: {:?}", &test);
    }
}
