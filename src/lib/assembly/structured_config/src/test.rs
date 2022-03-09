// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use assembly_structured_config::{validate_component, Repackager, ValidationError};
use assembly_validate_product::{validate_package, PackageValidationError};
use fuchsia_archive::Reader;
use fuchsia_pkg::{BlobInfo, PackageManifest};
use maplit::btreemap;
use std::{fs::File, io::Cursor};
use tempfile::TempDir;

const PASS_WITH_CONFIG: &str = "meta/pass_with_config.cm";
const PASS_WITHOUT_CONFIG: &str = "meta/pass_without_config.cm";
const FAIL_MISSING_CONFIG: &str = "meta/fail_missing_config.cm";

const TEST_MANIFEST_PATH: &str = env!("TEST_MANIFEST_PATH");

fn test_package_manifest() -> PackageManifest {
    serde_json::from_reader(File::open(TEST_MANIFEST_PATH).unwrap()).unwrap()
}

fn test_meta_far() -> Reader<Cursor<Vec<u8>>> {
    Reader::new(Cursor::new(std::fs::read(env!("TEST_META_FAR")).unwrap())).unwrap()
}

/// Makes sure that we can "turn a package valid" if it's been produced by the build without value
/// files.
#[test]
fn adding_config_makes_invalid_package_valid() {
    let original_manifest = test_package_manifest();

    // ensure that product validation will fail without us doing anything
    match validate_package(TEST_MANIFEST_PATH) {
        Err(PackageValidationError::InvalidComponents(..)) => (),
        other => panic!("expected validation to fail with invalid components, got {:#?}", other),
    }

    // provide config values for the previously-invalid component
    let temp = TempDir::new().unwrap();
    let mut repackager = Repackager::new(original_manifest.clone(), temp.path()).unwrap();
    repackager
        .set_component_config(FAIL_MISSING_CONFIG, btreemap! { "foo".to_string() => true.into() })
        .unwrap();
    let new_manifest_path = repackager.build().unwrap();

    // ensure that the modified package is valid
    validate_package(&new_manifest_path).expect("package must be valid after adding config");
}

/// Makes sure that the product assembly tooling never silently squashes an existing value file.
#[test]
fn cant_add_config_on_top_of_existing_values() {
    let original_manifest = test_package_manifest();
    let temp = TempDir::new().unwrap();
    let mut repackager = Repackager::new(original_manifest.clone(), temp.path()).unwrap();
    repackager
        .set_component_config(PASS_WITH_CONFIG, btreemap! { "foo".to_string() => true.into() })
        .unwrap_err();
}

/// Checks against unintended side effects from repackaging.
#[test]
fn repackaging_with_no_config_produces_identical_manifest() {
    let original_manifest = test_package_manifest();

    let temp = TempDir::new().unwrap();
    let repackager = Repackager::new(original_manifest.clone(), temp.path()).unwrap();
    let new_manifest_path = repackager.build().unwrap();

    let new_manifest: PackageManifest =
        serde_json::from_reader(File::open(&new_manifest_path).unwrap()).unwrap();

    assert_eq!(original_manifest.name(), new_manifest.name(), "repackaging must re-use pkg name");
    assert_eq!(
        original_manifest.blobs().len(),
        new_manifest.blobs().len(),
        "repackaging without config must not change # of blobs"
    );

    // test blobs for equality
    // (ignoring source paths because we wrote the new blob contents into a temporary directory)
    for (original_blob, new_blob) in
        original_manifest.blobs().iter().zip(new_manifest.blobs().iter())
    {
        let BlobInfo {
            source_path: _,
            path: original_path,
            merkle: original_merkle,
            size: original_size,
        } = original_blob;
        let BlobInfo { source_path: _, path: new_path, merkle: new_merkle, size: new_size } =
            new_blob;
        assert_eq!(original_path, new_path, "no-op repackaging should not change blob paths");
        assert_eq!(original_merkle, new_merkle, "no-op repackaging should not change blob merkles");
        assert_eq!(original_size, new_size, "no-op repackaging should not change blob sizes");
    }
}

#[test]
fn config_resolves() {
    validate_component(PASS_WITH_CONFIG, &mut test_meta_far()).unwrap();
}

#[test]
fn no_config_passes() {
    validate_component(PASS_WITHOUT_CONFIG, &mut test_meta_far()).unwrap();
}

#[test]
fn config_requires_values() {
    match validate_component(FAIL_MISSING_CONFIG, &mut test_meta_far()).unwrap_err() {
        ValidationError::ConfigValuesMissing { .. } => (),
        other => panic!("expected missing values, got {}", other),
    }
}
