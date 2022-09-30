// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    fuchsia_hash::Hash,
    fuchsia_pkg::{PackageManifest, PackagePath},
    fuchsia_url::{PinnedAbsolutePackageUrl, RepositoryUrl},
    serde_json::json,
    std::{collections::BTreeMap, fs::File, io::Write, path::Path, str::FromStr},
};

/// `WritablePackageList` represents a collection of packages that can be populated and
/// written into a file. This allows for gradual migration for packages index config
/// files (boot, base, cache) to JSON incrementally.
/// TODO(fxb/94601): refactor out once base_packages are migrated to JSON format.
pub trait WritablePackageList {
    /// Add a new package with `name` and `merkle`.
    fn insert(
        &mut self,
        repository: Option<impl AsRef<str>>,
        name: impl AsRef<str>,
        merkle: Hash,
    ) -> Result<()>;
    /// Generate the file to be used as the package index.
    fn write(&self, out: &mut impl Write) -> Result<()>;

    /// Returns whether the list has contents to write.
    fn is_empty(&self) -> bool;

    /// Pulls out the path and merkle from `package` and adds it to `packages` with a path to
    /// merkle mapping.
    fn add_package(&mut self, package: PackageManifest) -> Result<()> {
        let package_name = package.name().as_ref();
        if package_name == "system_image" || package_name == "update" {
            return Err(anyhow!("system_image and update packages are not allowed"));
        }

        let package_repository = package.repository();
        let path = package.package_path().to_string();
        package
            .blobs()
            .into_iter()
            .find(|blob| blob.path == "meta/")
            .ok_or(anyhow!("Failed to add package {} to the list, unable to find meta blob", path))
            .and_then(|meta_blob| self.insert(package_repository, path, meta_blob.merkle))
    }

    /// Helper fn to handle the (repeated) process of writing a list of packages
    /// out to the expected file, and returning a (destination, source) tuple
    /// for inclusion in the package's contents.
    fn write_index_file(
        &self,
        gendir: impl AsRef<Path>,
        name: &str,
        destination: impl AsRef<str>,
    ) -> Result<(String, String)> {
        // TODO(fxbug.dev/76326) Decide on a consistent pattern for using gendir and
        //   how intermediate files should be named and where in gendir they should
        //   be placed.
        //
        // For a file of destination "data/foo.txt", and a gendir of "assembly/gendir",
        //   this creates "assembly/gendir/data/foo.txt".
        let path = gendir.as_ref().join(destination.as_ref());
        let path_str = path
            .to_str()
            .ok_or(anyhow!(format!("package index path is not valid UTF-8: {}", path.display())))?;

        // Create any parent dirs necessary.
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).context(format!(
                "Failed to create parent dir {} for {} in gendir",
                parent.display(),
                destination.as_ref()
            ))?;
        }

        let mut index_file = File::create(&path)
            .context(format!("Failed to create the {} packages index file: {}", name, path_str))?;

        self.write(&mut index_file).context(format!(
            "Failed to write the {} package index file: {}",
            name,
            path.display()
        ))?;

        Ok((destination.as_ref().to_string(), path_str.to_string()))
    }
}

/// A list of mappings between package name and merkle, which can be written to
/// a file to be used as a package index.
#[derive(Default, Debug)]
pub struct PackageList {
    // Map between package name and merkle.
    packages: BTreeMap<String, Hash>,
}

impl WritablePackageList for PackageList {
    /// Add a new package with `name` and `merkle`.
    fn insert(
        &mut self,
        _repository: Option<impl AsRef<str>>,
        name: impl AsRef<str>,
        merkle: Hash,
    ) -> Result<()> {
        self.packages.insert(name.as_ref().to_string(), merkle);
        Ok(())
    }

    /// Generate the file to be used as a package index.
    fn write(&self, out: &mut impl Write) -> Result<()> {
        for (name, merkle) in self.packages.iter() {
            writeln!(out, "{}={}", name, merkle)?;
        }
        Ok(())
    }

    fn is_empty(&self) -> bool {
        self.packages.is_empty()
    }
}

/// A list of package URLs pinned to a hash, which can be written to a file.
#[derive(Default, Debug)]
pub struct PackageUrlList {
    packages: Vec<PinnedAbsolutePackageUrl>,
}

impl PackageUrlList {
    /// Returns a reference to the list absolute package urls
    /// that this instance contains.
    pub fn get_packages(&self) -> &Vec<PinnedAbsolutePackageUrl> {
        return &self.packages;
    }
}

impl WritablePackageList for PackageUrlList {
    /// Insert a new pinned to hash URL into the list.
    fn insert(
        &mut self,
        repository: Option<impl AsRef<str>>,
        name: impl AsRef<str>,
        merkle: Hash,
    ) -> Result<()> {
        let repository =
            repository.ok_or(anyhow!("Unable to create package url: empty repository field."))?;
        let path = PackagePath::from_str(name.as_ref())
            .map_err(|e| anyhow!("Failed to parse package path: {}", e))?;
        let url = PinnedAbsolutePackageUrl::new_with_path(
            RepositoryUrl::parse_host(repository.as_ref().to_string())
                .context("Failed to create repository url")?,
            &format!("/{}", path),
            merkle,
        )
        .map_err(|e| anyhow!("Failed to create package url: {}", e))?;
        self.packages.push(url);
        Ok(())
    }

    /// Generate the file to be placed in the Base Package.
    fn write(&self, writer: &mut impl Write) -> Result<()> {
        // If there are no packages, we should generate an empty file.
        if self.packages.is_empty() {
            return Ok(());
        }
        let contents = json!({
            "version": "1",
            "content": &self.packages,
        });
        serde_json::to_writer(writer, &contents).map_err(|e| anyhow!("Error writing JSON: {}", e))
    }

    fn is_empty(&self) -> bool {
        self.packages.is_empty()
    }
}

impl PartialEq<PackageList> for Vec<(String, Hash)> {
    fn eq(&self, other: &PackageList) -> bool {
        if self.len() == other.packages.len() {
            for item in self {
                match other.packages.get(&item.0) {
                    Some(hash) => {
                        if hash != &item.1 {
                            return false;
                        }
                    }
                    None => {
                        return false;
                    }
                }
            }
            return true;
        }
        return false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_pkg::{BlobInfo, MetaPackage, PackageManifest, PackageManifestBuilder};
    use std::path::Path;

    #[test]
    fn package_list() {
        let mut out: Vec<u8> = Vec::new();
        let mut packages = PackageList::default();
        packages.insert(Some("testrepository.com"), "package0", Hash::from([0u8; 32])).unwrap();
        packages.insert(Some("testrepository.com"), "package1", Hash::from([17u8; 32])).unwrap();
        packages.write(&mut out).unwrap();
        assert_eq!(
            b"package0=0000000000000000000000000000000000000000000000000000000000000000\n\
                    package1=1111111111111111111111111111111111111111111111111111111111111111\n",
            &*out
        );
    }

    #[test]
    fn package_url_list() {
        let mut out: Vec<u8> = Vec::new();
        let mut packages = PackageUrlList::default();
        packages.insert(Some("testrepository.com"), "package0/0", Hash::from([0u8; 32])).unwrap();
        packages.insert(Some("testrepository.com"), "package1/0", Hash::from([17u8; 32])).unwrap();
        packages.write(&mut out).unwrap();
        assert_eq!(
            br#"{"content":["fuchsia-pkg://testrepository.com/package0/0?hash=0000000000000000000000000000000000000000000000000000000000000000","fuchsia-pkg://testrepository.com/package1/0?hash=1111111111111111111111111111111111111111111111111111111111111111"],"version":"1"}"#,
            &*out
        );
    }

    #[test]
    fn empty_package_url_list() {
        let mut out: Vec<u8> = Vec::new();
        let packages = PackageUrlList::default();
        packages.write(&mut out).unwrap();
        assert_eq!(b"", &*out);
    }

    #[test]
    fn test_add_package_to() {
        let system_image = generate_test_manifest("system_image", None);
        let update = generate_test_manifest("update", None);
        let valid = generate_test_manifest("valid", None);
        let mut packages = PackageList::default();
        assert!(WritablePackageList::add_package(&mut packages, system_image).is_err());
        assert!(WritablePackageList::add_package(&mut packages, update).is_err());
        assert!(WritablePackageList::add_package(&mut packages, valid).is_ok());
    }

    #[test]
    fn test_add_package_to_url_list() {
        let system_image = generate_test_manifest("system_image", None);
        let update = generate_test_manifest("update", None);
        let valid = generate_test_manifest("valid", None);
        let mut packages = PackageUrlList::default();
        assert!(WritablePackageList::add_package(&mut packages, system_image).is_err());
        assert!(WritablePackageList::add_package(&mut packages, update).is_err());
        assert!(WritablePackageList::add_package(&mut packages, valid).is_ok());
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
        let builder = PackageManifestBuilder::new(MetaPackage::from_name(name.parse().unwrap()));
        let builder = builder.repository("testrepository.com");
        let builder = builder.add_blob(BlobInfo {
            source_path: meta_source,
            path: "meta/".into(),
            merkle: "0000000000000000000000000000000000000000000000000000000000000000"
                .parse()
                .unwrap(),
            size: 1,
        });
        let builder = builder.add_blob(BlobInfo {
            source_path: file_source,
            path: "data/file.txt".into(),
            merkle: "1111111111111111111111111111111111111111111111111111111111111111"
                .parse()
                .unwrap(),
            size: 1,
        });
        builder.build()
    }
}
