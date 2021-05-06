// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error, Result};
use fuchsia_pkg::{CreationManifest, MetaPackage, PackageManifest};
use std::collections::BTreeMap;
use std::fs::File;
use std::io::Write;
use std::path::Path;

/// A builder that constructs base packages.
#[derive(Default)]
pub struct BasePackageBuilder {
    // Maps the blob destination -> source.
    contents: BTreeMap<String, String>,
    base_packages: PackageList,
    cache_packages: PackageList,
}

impl BasePackageBuilder {
    /// Add all the blobs from `package` into the base package being built.
    pub fn add_files_from_package(&mut self, package: PackageManifest) {
        package.into_blobs().into_iter().filter(|b| b.path != "meta/").for_each(|b| {
            self.contents.insert(b.path, b.source_path);
        });
    }

    /// Add the `package` to the list of base packages, which is then added to
    /// base package as file `data/static_packages`.
    pub fn add_base_package(&mut self, package: PackageManifest) {
        add_package_to(&mut self.base_packages, package);
    }

    /// Add the `package` to the list of cache packages, which is then added to
    /// base package as file `data/cache_packages`.
    pub fn add_cache_package(&mut self, package: PackageManifest) {
        add_package_to(&mut self.cache_packages, package);
    }

    /// Build the base package and write the bytes to `out`.
    pub fn build(self, gendir: impl AsRef<Path>, out: &mut impl Write) -> Result<()> {
        // Generate the base and cache package lists.
        let base_packages_path = gendir.as_ref().join("base_packages.list");
        let cache_packages_path = gendir.as_ref().join("cache_packages.list");
        let mut base_packages = File::create(&base_packages_path)
            .map_err(|e| Error::new(e).context("failed to create the base packages list"))?;
        let mut cache_packages = File::create(&cache_packages_path)
            .map_err(|e| Error::new(e).context("failed to create the cache packages list"))?;
        self.base_packages.write(&mut base_packages)?;
        self.cache_packages.write(&mut cache_packages)?;

        // Ensure the base/cache file paths are valid UTF-8.
        let base_packages_path = base_packages_path.to_str().ok_or(anyhow!(format!(
            "Base package list is not valid UTF-8: {}",
            base_packages_path.display()
        )))?;
        let cache_packages_path = cache_packages_path.to_str().ok_or(anyhow!(format!(
            "Cache package list is not valid UTF-8: {}",
            cache_packages_path.display()
        )))?;

        // Construct the list of blobs in the base package that lives outside of the meta.far.
        let mut external_contents = self.contents;
        external_contents
            .insert("data/static_packages".to_string(), base_packages_path.to_string());
        external_contents
            .insert("data/cache_packages".to_string(), cache_packages_path.to_string());

        // The base package does not have any files inside the meta.far.
        let far_contents = BTreeMap::new();

        // Build the base packages.
        let creation_manifest =
            CreationManifest::from_external_and_far_contents(external_contents, far_contents)?;
        let meta_package = MetaPackage::from_name_and_variant("system_image", "0")?;
        fuchsia_pkg::build(&creation_manifest, &meta_package, out)?;

        Ok(())
    }
}

// Pulls out the name and merkle from `package` and adds it to `packages` with a name to
// merkle mapping.
fn add_package_to(list: &mut PackageList, package: PackageManifest) {
    let name = package.name().to_string();
    let meta_blob = package.into_blobs().into_iter().find(|blob| blob.path == "meta/");
    match meta_blob {
        Some(meta_blob) => {
            list.insert(name, meta_blob.merkle.to_string());
        }
        _ => {
            println!("Failed to add package {} to the list", name);
        }
    }
}

/// A list of mappings between package name and merkle, which can be written to
/// a file to be placed in the Base Package.
#[derive(Default)]
struct PackageList {
    // Map between package name and merkle.
    packages: BTreeMap<String, String>,
}

impl PackageList {
    /// Add a new package with `name` and `merkle`.
    fn insert(&mut self, name: impl AsRef<str>, merkle: impl AsRef<str>) {
        self.packages.insert(name.as_ref().to_string(), merkle.as_ref().to_string());
    }

    /// Generate the file to be placed in the Base Package.
    fn write(&self, out: &mut impl Write) -> Result<()> {
        for (name, merkle) in self.packages.iter() {
            write!(out, "{}={}\n", name, merkle)?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_archive::Reader;
    use serde_json::json;
    use std::io::Cursor;
    use std::path::Path;
    use tempfile::{NamedTempFile, TempDir};

    #[test]
    fn package_list() {
        let mut out: Vec<u8> = Vec::new();
        let mut packages = PackageList::default();
        packages
            .insert("package0", "0000000000000000000000000000000000000000000000000000000000000000");
        packages
            .insert("package1", "1111111111111111111111111111111111111111111111111111111111111111");
        packages.write(&mut out).unwrap();
        assert_eq!(
            out,
            b"package0=0000000000000000000000000000000000000000000000000000000000000000\n\
                    package1=1111111111111111111111111111111111111111111111111111111111111111\n"
        );
    }

    #[test]
    fn build() {
        // Build the base package with an extra file, a base package, and a cache package.
        let mut far_bytes: Vec<u8> = Vec::new();
        let mut builder = BasePackageBuilder::default();
        let test_file = NamedTempFile::new().unwrap();
        builder.add_files_from_package(generate_test_manifest("package", Some(test_file.path())));
        builder.add_base_package(generate_test_manifest("base_package", None));
        builder.add_cache_package(generate_test_manifest("cache_package", None));

        let gen_dir = TempDir::new().unwrap();
        builder.build(&gen_dir.path(), &mut far_bytes).unwrap();

        // Read the output and ensure it contains the right files.
        let mut far_reader = Reader::new(Cursor::new(far_bytes)).unwrap();
        let package = far_reader.read_file("meta/package").unwrap();
        assert_eq!(package, br#"{"name":"system_image","version":"0"}"#);
        let contents = far_reader.read_file("meta/contents").unwrap();
        let contents = std::str::from_utf8(&contents).unwrap();
        let expected_contents = "\
            data/cache_packages=da77aea7e4d4382832190eab31d5ed1c388d8c16566145fdead5fbf3e426efdd\n\
            data/file.txt=15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b\n\
            data/static_packages=ad4c242ac15236cd7c2bbe6e89c3ddcabb3f4256c89a281503d0b61c926c3d09\n\
        "
        .to_string();
        assert_eq!(contents, expected_contents);
    }

    // Generates a package manifest to be used for testing. The `name` is used in the blob file
    // names to make each manifest somewhat unique. If supplied, `file_path` will be used as the
    // non-meta-far blob source path, which allows the tests to use a real file.
    fn generate_test_manifest(name: &str, file_path: Option<&Path>) -> PackageManifest {
        let meta_source = format!("path/to/{}/meta.far", name);
        let file_source = match file_path {
            Some(path) => path.to_string_lossy().into_owned(),
            _ => format!("path/to/{}/file.txt", name),
        };
        serde_json::from_value::<PackageManifest>(json!(
            {
                "version": "1",
                "package": {
                    "name": name,
                    "version": "0"
                },
                "blobs": [
                    {
                        "source_path": meta_source,
                        "path": "meta/",
                        "merkle":
                            "0000000000000000000000000000000000000000000000000000000000000000",
                        "size": 1
                    },

                    {
                        "source_path": file_source,
                        "path": "data/file.txt",
                        "merkle":
                            "1111111111111111111111111111111111111111111111111111111111111111",
                        "size": 1
                    },
                ]
            }
        ))
        .expect("valid json")
    }
}
