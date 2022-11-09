// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use assembly_base_package::BasePackageBuilder;
use assembly_config_schema::ImageAssemblyConfig;
use assembly_manifest::{AssemblyManifest, Image};
use assembly_util::path_relative_from_current_dir;
use camino::{Utf8Path, Utf8PathBuf};
use fuchsia_hash::Hash;
use fuchsia_merkle::MerkleTree;
use fuchsia_pkg::PackageManifest;
use std::collections::BTreeMap;
use std::fs::File;
use tracing::info;

pub struct BasePackage {
    pub merkle: Hash,
    pub contents: BTreeMap<String, String>,
    pub path: Utf8PathBuf,
    pub manifest_path: Utf8PathBuf,
}

pub fn construct_base_package(
    assembly_manifest: &mut AssemblyManifest,
    outdir: impl AsRef<Utf8Path>,
    gendir: impl AsRef<Utf8Path>,
    name: impl AsRef<str>,
    product: &ImageAssemblyConfig,
) -> Result<BasePackage> {
    let outdir = outdir.as_ref().join("base");
    if !outdir.exists() {
        std::fs::create_dir_all(&outdir)?;
    }

    let mut base_pkg_builder = BasePackageBuilder::default();
    for pkg_manifest_path in &product.system {
        let pkg_manifest = PackageManifest::try_load_from(pkg_manifest_path)?;
        base_pkg_builder.add_files_from_package(pkg_manifest);
    }
    for pkg_manifest_path in &product.base {
        let pkg_manifest = PackageManifest::try_load_from(pkg_manifest_path)?;
        base_pkg_builder.add_base_package(pkg_manifest).context(format!(
            "Failed to add package to base package list with manifest: {}",
            pkg_manifest_path
        ))?;
    }
    for pkg_manifest_path in &product.cache {
        let pkg_manifest = PackageManifest::try_load_from(pkg_manifest_path)?;
        base_pkg_builder.add_cache_package(pkg_manifest).context(format!(
            "Failed to add package to cache package list with manifest: {}",
            pkg_manifest_path
        ))?;
    }

    let base_package_path = outdir.join("meta.far");
    let build_results = base_pkg_builder
        .build(&outdir, gendir, name, &base_package_path)
        .context("Failed to build the base package")?;

    let base_package = File::open(&base_package_path).context("Failed to open the base package")?;
    let base_merkle = MerkleTree::from_reader(&base_package)
        .context("Failed to calculate the base merkle")?
        .root();
    info!("Base merkle: {}", &base_merkle);

    // Write the merkle to a file.
    let merkle_path = outdir.join("base.merkle");
    std::fs::write(merkle_path, hex::encode(base_merkle.as_bytes()))?;

    let base_package_path_relative = path_relative_from_current_dir(base_package_path.clone())?;
    assembly_manifest.images.push(Image::BasePackage(base_package_path_relative.clone()));
    Ok(BasePackage {
        merkle: base_merkle,
        contents: build_results.contents,
        path: base_package_path_relative,
        manifest_path: build_results.manifest_path,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    use assembly_util::path_relative_from_current_dir;
    use fuchsia_archive::Utf8Reader;
    use serde_json::json;
    use std::fs::File;
    use std::io::Write;
    use tempfile::tempdir;

    #[test]
    fn construct() {
        let tmp = tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        // Prepare package manifests to add to the base package.
        let system_manifest = generate_test_manifest_file(&dir, "extra_base");
        let base_manifest = generate_test_manifest_file(&dir, "test_static");
        let cache_manifest = generate_test_manifest_file(&dir, "test_cache");
        let mut product_config = ImageAssemblyConfig::new_for_testing("kernel", 0);
        product_config.system.push(system_manifest);
        product_config.base.push(base_manifest);
        product_config.cache.push(cache_manifest);

        // Construct the base package.
        let mut assembly_manifest = AssemblyManifest::default();
        let base_package = construct_base_package(
            &mut assembly_manifest,
            dir,
            dir,
            "system_image",
            &product_config,
        )
        .unwrap();
        assert_eq!(
            base_package.path,
            path_relative_from_current_dir(dir.join("base/meta.far")).unwrap()
        );

        // Read the base package, and assert the contents are correct.
        let base_package_file = File::open(base_package.path).unwrap();
        let mut far_reader = Utf8Reader::new(&base_package_file).unwrap();
        let contents = far_reader.read_file("meta/contents").unwrap();
        let contents = std::str::from_utf8(&contents).unwrap();
        let expected_contents = "\
            data/cache_packages.json=8a7bb95ce6dfb4f9c9b9b3c3b654923c4148351db2719ecc98d24ae328128e2b\n\
            data/static_packages=e4f7b124a8c758488b7b9f0a42c09cb0c3c3ca6e4cf2d42ba6f03158eb2ad890\n\
            extra_base_data.txt=6ef2ad21fe7a1f22e224da891fba56b8cc53f39b977867a839584d4cc3919c4c\n\
        "
        .to_string();
        assert_eq!(contents, expected_contents);
    }

    #[test]
    fn construct_prime() {
        let tmp = tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        // Prepare package manifests to add to the base package.
        let system_manifest = generate_test_manifest_file(&dir, "extra_base");
        let base_manifest = generate_test_manifest_file(&dir, "test_static");
        let cache_manifest = generate_test_manifest_file(&dir, "test_cache");
        let mut product_config = ImageAssemblyConfig::new_for_testing("kernel", 0);
        product_config.system.push(system_manifest);
        product_config.base.push(base_manifest);
        product_config.cache.push(cache_manifest);

        // Construct the base package.
        let mut assembly_manifest = AssemblyManifest::default();
        let base_package = construct_base_package(
            &mut assembly_manifest,
            dir,
            dir,
            "system_image",
            &product_config,
        )
        .unwrap();
        assert_eq!(
            base_package.path,
            path_relative_from_current_dir(dir.join("base/meta.far")).unwrap()
        );

        // Read the base package, and assert the contents are correct.
        let base_package_file = File::open(base_package.path).unwrap();
        let mut far_reader = Utf8Reader::new(&base_package_file).unwrap();
        let contents = far_reader.read_file("meta/package").unwrap();
        let contents = std::str::from_utf8(&contents).unwrap();

        // The name should still be "system_image" even for prime.
        let expected_contents = r#"{"name":"system_image","version":"0"}"#.to_string();
        assert_eq!(contents, expected_contents);
    }

    // Generates a package manifest to be used for testing. The file is written
    // into `dir`, and the location is returned. The `name` is used in the blob
    // file names to make each manifest somewhat unique.
    // TODO(fxbug.dev/76993): See if we can share this with BasePackage.
    pub fn generate_test_manifest_file(
        dir: impl AsRef<Utf8Path>,
        name: impl AsRef<str>,
    ) -> Utf8PathBuf {
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
                    "repository": "testrepository.com",
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
