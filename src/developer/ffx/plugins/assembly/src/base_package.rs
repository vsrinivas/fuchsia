// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config::ProductConfig;
use crate::util::pkg_manifest_from_path;

use anyhow::{Context, Result};
use assembly_base_package::BasePackageBuilder;
use fuchsia_hash::Hash;
use fuchsia_merkle::MerkleTree;
use log::info;
use std::collections::BTreeMap;
use std::fs::File;
use std::path::{Path, PathBuf};

pub struct BasePackage {
    pub merkle: Hash,
    pub contents: BTreeMap<String, String>,
    pub path: PathBuf,
}

pub fn construct_base_package(
    outdir: impl AsRef<Path>,
    gendir: impl AsRef<Path>,
    product: &ProductConfig,
) -> Result<BasePackage> {
    let mut base_pkg_builder = BasePackageBuilder::default();
    for pkg_manifest_path in &product.extra_packages_for_base_package {
        let pkg_manifest = pkg_manifest_from_path(pkg_manifest_path)?;
        base_pkg_builder.add_files_from_package(pkg_manifest);
    }
    for pkg_manifest_path in &product.base_packages {
        let pkg_manifest = pkg_manifest_from_path(pkg_manifest_path)?;
        base_pkg_builder.add_base_package(pkg_manifest).context(format!(
            "Failed to add package to base package list with manifest: {}",
            pkg_manifest_path.display()
        ))?;
    }
    for pkg_manifest_path in &product.cache_packages {
        let pkg_manifest = pkg_manifest_from_path(pkg_manifest_path)?;
        base_pkg_builder.add_cache_package(pkg_manifest).context(format!(
            "Failed to add package to cache package list with manifest: {}",
            pkg_manifest_path.display()
        ))?;
    }

    let base_package_path = outdir.as_ref().join("base.far");
    let build_results = base_pkg_builder
        .build(outdir, gendir, &base_package_path)
        .context("Failed to build the base package")?;

    let base_package = File::open(&base_package_path).context("Failed to open the base package")?;
    let base_merkle = MerkleTree::from_reader(&base_package)
        .context("Failed to calculate the base merkle")?
        .root();
    info!("Base merkle: {}", &base_merkle);

    Ok(BasePackage {
        merkle: base_merkle,
        contents: build_results.contents,
        path: base_package_path,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::config::ProductConfig;
    use fuchsia_archive::Reader;
    use serde_json::json;
    use std::fs::File;
    use std::io::Write;
    use tempfile::tempdir;

    #[test]
    fn construct() {
        let dir = tempdir().unwrap();

        // Prepare package manifests to add to the base package.
        let mut product_config = ProductConfig::default();
        let manifest = generate_test_manifest_file(&dir.path(), "extra_base");
        product_config.extra_packages_for_base_package.push(manifest);
        let manifest = generate_test_manifest_file(&dir.path(), "test_static");
        product_config.base_packages.push(manifest);
        let manifest = generate_test_manifest_file(&dir.path(), "test_cache");
        product_config.cache_packages.push(manifest);

        // Construct the base package.
        let base_package = construct_base_package(dir.path(), dir.path(), &product_config).unwrap();
        assert_eq!(base_package.path, dir.path().join("base.far"));

        // Read the base package, and assert the contents are correct.
        let base_package_file = File::open(base_package.path).unwrap();
        let mut far_reader = Reader::new(&base_package_file).unwrap();
        let contents = far_reader.read_file("meta/contents").unwrap();
        let contents = std::str::from_utf8(&contents).unwrap();
        let expected_contents = "\
            data/cache_packages=7740f21364ff0b21f83cb671c2684d08e5d6b64186de66e5b622cef2fd8402b2\n\
            data/static_packages=e4f7b124a8c758488b7b9f0a42c09cb0c3c3ca6e4cf2d42ba6f03158eb2ad890\n\
            extra_base_data.txt=6ef2ad21fe7a1f22e224da891fba56b8cc53f39b977867a839584d4cc3919c4c\n\
        "
        .to_string();
        assert_eq!(contents, expected_contents);
    }

    // Generates a package manifest to be used for testing. The file is written
    // into `dir`, and the location is returned. The `name` is used in the blob
    // file names to make each manifest somewhat unique.
    // TODO(fxbug.dev/76993): See if we can share this with BasePackage.
    pub fn generate_test_manifest_file(dir: impl AsRef<Path>, name: impl AsRef<str>) -> PathBuf {
        // Create a data file for the package.
        let data_file_name = format!("{}_data.txt", name.as_ref());
        let data_path = dir.as_ref().join(&data_file_name);
        let data_file = File::create(&data_path).unwrap();
        write!(&data_file, "bleh").unwrap();

        // Create the manifest.
        let manifest_path = dir.as_ref().join(format!("{}.json", name.as_ref()));
        let manifest_file = File::create(&manifest_path).unwrap();
        serde_json::to_writer(
            &manifest_file,
            &json!({
                    "version": "1",
                    "package": {
                        "name": name.as_ref(),
                        "version": "1",
                    },
                    "blobs": [
                        {
                            "source_path": format!("path/to/{}/meta.far", name.as_ref()),
                            "path": "meta/",
                            "merkle":
                                "0000000000000000000000000000000000000000000000000000000000000000",
                            "size": 1
                        },
                        {
                            "source_path": &data_path,
                            "path": &data_file_name,
                            "merkle":
                                "1111111111111111111111111111111111111111111111111111111111111111",
                            "size": 1
                        },
                    ]
                }
            ),
        )
        .unwrap();
        manifest_path
    }
}
