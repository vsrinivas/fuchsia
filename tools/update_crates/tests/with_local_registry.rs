// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This test exercises the `update_crates` tool against a golden project.
//!
//! To add "test cases," define new crates in `./local_registry_sources` and include them in
//! `./BUILD.gn` under `uses_local_registry_test_data`'s `sources` to ensure they're copied to CQ's
//! test runners. Any crates in `./local_registry_sources` on the test runner will be included in
//! the custom local registry used for update queries.
//!
//! Once the crates are in the test registry, depend on those crates in
//! `./uses_local_registry/Cargo.toml` and add the expected post-update state to
//! `./uses_local_registry/Cargo.expected.toml`.
//!
//! The `update_crates` tool can also be configured at `./uses_local_registry/outdated.toml`.

use argh::FromArgs;
use crypto::{digest::Digest, sha2::Sha256};
use once_cell::sync::Lazy;
use serde::{Deserialize, Serialize};
use std::{
    collections::{BTreeMap, BTreeSet},
    env,
    ffi::OsStr,
    fs::File,
    io::Write,
    iter,
    path::{Path, PathBuf},
    process::Command,
    sync::Mutex,
};
use tempfile::TempDir;
use walkdir::WalkDir;

/// an integration test for the update_crates host tool
#[derive(Debug, FromArgs)]
struct TestArgs {
    /// path to the tests directory
    #[argh(option)]
    test_base_dir: PathBuf,
    /// path to the bin/ dir within our rust prebuilt distro
    #[argh(option)]
    rust_bin_dir: PathBuf,
    /// path to prebuilt cargo-outdated
    #[argh(option)]
    cargo_outdated: PathBuf,
    /// path to update_crates binary to test
    #[argh(option)]
    update_crates: PathBuf,
}

impl TestArgs {
    /// Get absolute paths for each of these so we can pass them to subprocesses with different
    /// working directories than our own.
    fn canonicalize(mut self) -> Self {
        self.test_base_dir = std::fs::canonicalize(self.test_base_dir).unwrap();
        self.rust_bin_dir = std::fs::canonicalize(self.rust_bin_dir).unwrap();
        self.cargo_outdated = std::fs::canonicalize(self.cargo_outdated).unwrap();
        self.update_crates = std::fs::canonicalize(self.update_crates).unwrap();
        self
    }
}

fn main() {
    let TestArgs { test_base_dir, rust_bin_dir, cargo_outdated, update_crates } =
        argh::from_env::<TestArgs>().canonicalize();

    // copy everything to a temporary directory and shadow variable name so we don't modify source
    let test_base_dir = setup_test_directory(test_base_dir);

    // add our rust distribution to our PATH
    let existing_path = env::var("PATH").unwrap();
    let new_path =
        env::join_paths(iter::once(rust_bin_dir.clone()).chain(env::split_paths(&existing_path)))
            .unwrap();
    env::set_var("PATH", new_path);

    let test_project_root = test_base_dir.join("uses_local_registry");
    // remove potentially stale lockfile in case of hash collisions during development
    std::fs::remove_file(test_project_root.join("Cargo.lock")).ok();

    // populate the local registry
    let registry_path = test_base_dir.join("registry");
    let config_contents =
        make_test_registry(test_base_dir.join("local_registry_sources"), &registry_path);
    // populate the `.cargo/config.toml` which overrides with our local registry
    let dot_cargo = test_project_root.join(".cargo");
    std::fs::create_dir_all(&dot_cargo).unwrap();
    std::fs::write(dot_cargo.join("config.toml"), config_contents).unwrap();

    // run the update tool
    let test_project_manifest = test_project_root.join("Cargo.toml");
    Command::new(update_crates)
        .arg("--manifest-path")
        .arg(&test_project_manifest)
        .arg("--overrides")
        .arg(test_project_root.join("outdated.toml"))
        .arg("update")
        .arg("--cargo")
        .arg(rust_bin_dir.join("cargo"))
        .arg("--outdated-dir")
        .arg(cargo_outdated.parent().unwrap())
        .arg("--offline")
        // use a temp directory so that the workstation environment is close to CQ
        .env("CARGO_HOME", test_base_dir.join("cargo_home"))
        // we need to set cwd so that cargo-outdated picks up the .cargo/config.toml we wrote
        // (this is why we need to canonicalize the args above)
        .current_dir(&test_project_root)
        .output()
        .unwrap_success();

    // make sure the tool did what we expect
    let observed_manifest_after_update = std::fs::read_to_string(test_project_manifest).unwrap();
    let expected_manifest_after_update =
        std::fs::read_to_string(test_project_root.join("Cargo.expected.toml")).unwrap();
    assert_eq!(observed_manifest_after_update, expected_manifest_after_update);
}

fn setup_test_directory(test_source_dir: PathBuf) -> PathBuf {
    /// We put the temp dir in a static so that a panic can suppress its cleanup routine.
    static TEST_DIR: Lazy<Mutex<Option<TempDir>>> = Lazy::new(|| Mutex::new(None));

    let temp_test_dir = TempDir::new().unwrap();
    let output_path = temp_test_dir.path().to_owned();
    *TEST_DIR.lock().unwrap() = Some(temp_test_dir);

    // install a panic hook that will leave the directory in place, printing the path
    let prev_panic_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        if let Some(temp_test_dir) = TEST_DIR.lock().unwrap().take() {
            let temp_path = temp_test_dir.into_path(); // avoids the cleanup dtor
            eprintln!("left test directory persisted at {}", temp_path.display());
        }
        prev_panic_hook(info);
    }));

    // copy everything from test_source_dir to output_path
    for entry in WalkDir::new(&test_source_dir) {
        let entry = entry.unwrap();
        let source = entry.path();
        let suffix = source.strip_prefix(&test_source_dir).unwrap();
        let target = output_path.join(suffix);

        if let Err(e) = if source.is_file() {
            std::fs::copy(source, &target).map(|_| ())
        } else if source.is_dir() {
            std::fs::create_dir_all(&target)
        } else {
            unreachable!("no special files should be in test source directory");
        } {
            panic!("copying {} to {} failed: {}", source.display(), target.display(), e);
        }
    }

    output_path
}

trait UnwrapSuccess {
    #[track_caller]
    fn unwrap_success(self);
}

impl<E: std::fmt::Debug> UnwrapSuccess for Result<std::process::Output, E> {
    fn unwrap_success(self) {
        let output = self.unwrap();
        if !output.status.success() {
            panic!(
                "command failed: {}\nstdout:\n{}\nstderr:\n{}",
                output.status,
                String::from_utf8_lossy(&output.stdout),
                String::from_utf8_lossy(&output.stderr)
            )
        }
    }
}

/// Creates a test registry at the provided path, returning the contents of a `.cargo/config.toml`
/// that makes use of it.
fn make_test_registry(sources: PathBuf, registry_path: &Path) -> String {
    std::fs::remove_dir_all(&registry_path).ok(); // this will fail if this is a clean builder

    let mut packages: BTreeMap<PathBuf, IndexEntry> = Default::default();
    for entry in std::fs::read_dir(sources).unwrap() {
        let manifest = entry.unwrap().path().join("Cargo.toml");

        let (package_name, version) = CrateVersion::new(manifest);
        let index_file_path = registry_path.join("index").join(index_subpath(&package_name));

        packages.entry(index_file_path).or_default().versions.insert(version);
    }

    for (index_file_path, entry) in packages {
        entry.populate_in_index(&registry_path, &index_file_path);
    }

    format!(
        "\
            [source.crates-io]
            registry = 'https://github.com/rust-lang/crates.io-index'
            replace-with = 'local-registry'

            [source.local-registry]
            local-registry = '{}'
            ",
        registry_path.display()
    )
}

#[derive(Clone, Debug, Default, Eq, Hash, PartialEq, PartialOrd, Ord)]
struct IndexEntry {
    versions: BTreeSet<CrateVersion>,
}

impl IndexEntry {
    fn populate_in_index(self, registry_path: &Path, destination: &Path) {
        std::fs::create_dir_all(destination.parent().unwrap()).unwrap();
        let mut index_file = File::create(destination).unwrap();
        for version in self.versions {
            // add a line to the json file
            serde_json::to_writer(&mut index_file, &version.metadata).unwrap();
            index_file.write(b"\n").unwrap();

            // copy the .crate file to the registry
            let crate_destination = registry_path.join(version.crate_source.file_name().unwrap());
            std::fs::copy(&version.crate_source, crate_destination).unwrap();
        }
    }
}

#[derive(Clone, Debug, Eq, Hash, PartialEq, PartialOrd, Ord)]
struct CrateVersion {
    version: String,
    crate_source: PathBuf,
    metadata: VersionMetadata,
}

impl CrateVersion {
    /// runs `cargo package` on the manifest and returns the name of the package and a path to the
    /// `.crate` file produced
    fn new(manifest_path: PathBuf) -> (String, Self) {
        Command::new("cargo")
            .arg("package")
            .arg("--allow-dirty")
            .arg("--manifest-path")
            .arg(&manifest_path)
            .output()
            .unwrap_success();

        let package_dir = manifest_path.parent().unwrap().join("target").join("package");
        let crate_source = std::fs::read_dir(package_dir)
            .unwrap()
            .map(|e| e.unwrap().path().to_owned())
            .filter(|p| p.extension() == Some(OsStr::new("crate")))
            .next()
            .unwrap();

        let manifest_contents = std::fs::read_to_string(&manifest_path).unwrap();
        let manifest: toml::Value = toml::from_str(&manifest_contents).unwrap();
        let package_name = manifest["package"]["name"].as_str().unwrap().to_string();
        let version = manifest["package"]["version"].as_str().unwrap().to_string();

        let crate_file_contents = std::fs::read(&crate_source).unwrap();

        let mut digest = Sha256::new();
        digest.input(&crate_file_contents);
        let cksum = digest.result_str();

        let metadata = VersionMetadata {
            name: package_name.clone(),
            vers: version.clone(),
            deps: vec![],
            cksum,
            features: Default::default(),
            yanked: false,
            links: None,
        };

        (package_name, Self { crate_source, version, metadata })
    }
}

/// from https://doc.rust-lang.org/cargo/reference/registries.html:
///
/// Each line in a package file contains a JSON object that describes a published version of the
/// package.
#[derive(Clone, Debug, Eq, Hash, PartialEq, PartialOrd, Ord, Deserialize, Serialize)]
struct VersionMetadata {
    /// The name of the package. This must only contain alphanumeric, `-`, or `_` characters.
    name: String,
    /// The version of the package this row is describing. This must be a valid version number
    /// according to the Semantic Versioning 2.0.0 spec at https://semver.org/.
    vers: String,
    /// Array of direct dependencies of the package.
    deps: Vec<DependencyMetadata>,
    /// A SHA256 checksum of the `.crate` file.
    cksum: String,
    /// Set of features defined for the package. Each feature maps to an array of features or
    /// dependencies it enables.
    features: BTreeMap<String, Vec<String>>,
    /// Boolean of whether or not this version has been yanked.
    yanked: bool,
    /// The `links` string value from the package's manifest, or null if not specified. This field
    /// is optional and defaults to null.
    links: Option<String>,
}

#[allow(unused)]
#[derive(Clone, Debug, Eq, Hash, PartialEq, PartialOrd, Ord, Deserialize, Serialize)]
struct DependencyMetadata {
    /// Name of the dependency.
    /// If the dependency is renamed from the original package name,
    /// this is the new name. The original package name is stored in
    /// the `package` field.
    name: String,
    /// The semver requirement for this dependency.
    /// This must be a valid version requirement defined at
    /// https://github.com/steveklabnik/semver#requirements.
    req: String,
    /// Array of features (as strings) enabled for this dependency.
    features: Vec<String>,
    /// Boolean of whether or not this is an optional dependency.
    optional: bool,
    /// Boolean of whether or not default features are enabled.
    default_features: bool,
    /// The target platform for the dependency.
    /// null if not a target dependency.
    /// Otherwise, a string such as "cfg(windows)".
    target: Option<String>,
    /// The dependency kind.
    /// "dev", "build", or "normal".
    /// Note: this is a required field, but a small number of entries
    /// exist in the crates.io index with either a missing or null
    /// `kind` field due to implementation bugs.
    kind: DepKind,
    /// The URL of the index of the registry where this dependency is
    /// from as a string. If not specified or null, it is assumed the
    /// dependency is in the current registry.
    registry: Option<String>,
    /// If the dependency is renamed, this is a string of the actual
    /// package name. If not specified or null, this dependency is not
    /// renamed.
    package: Option<String>,
}

#[allow(unused)]
#[derive(Clone, Debug, Eq, Hash, PartialEq, PartialOrd, Ord, Deserialize, Serialize)]
#[serde(rename_all = "lowercase")]
enum DepKind {
    Dev,
    Build,
    Normal,
}

/// from https://doc.rust-lang.org/cargo/reference/registries.html:
///
/// The rest of the index repository contains one file for each package, where the filename is the
/// name of the package in lowercase. Each version of the package has a separate line in the file.
/// The files are organized in a tier of directories:
///
/// * Packages with 1 character names are placed in a directory named `1`.
/// * Packages with 2 character names are placed in a directory named `2`.
/// * Packages with 3 character names are placed in the directory `3/{first-character}` where
///   `{first-character}` is the first character of the package name.
/// * All other packages are stored in directories named `{first-two}/{second-two}` where the top
///   directory is the first two characters of the package name, and the next subdirectory is the
///   third and fourth characters of the package name. For example, `cargo` would be stored in a
///   file named `ca/rg/cargo`.
fn index_subpath(package_name: &str) -> PathBuf {
    let package_name = package_name.to_ascii_lowercase();
    match package_name.len() {
        0 => unreachable!("disallowed by cargo's rules"),
        1 | 2 | 3 => unreachable!("requires special behavior not needed for this test"),
        _ => {
            let first_two = package_name.split_at(2).0;
            let second_two = package_name.split_at(4).0.split_at(2).1;
            PathBuf::from(first_two).join(second_two)
        }
        .join(package_name),
    }
}
