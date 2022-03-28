// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{MetaPackage, Package, PackageManifestError, PackageName, PackagePath, PackageVariant},
    serde::{Deserialize, Serialize},
};

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(transparent)]
pub struct PackageManifest(VersionedPackageManifest);

impl PackageManifest {
    pub fn blobs(&self) -> &[BlobInfo] {
        match &self.0 {
            VersionedPackageManifest::Version1(manifest) => &manifest.blobs,
        }
    }

    pub fn into_blobs(self) -> Vec<BlobInfo> {
        match self.0 {
            VersionedPackageManifest::Version1(manifest) => manifest.blobs,
        }
    }

    pub fn name(&self) -> &PackageName {
        match &self.0 {
            VersionedPackageManifest::Version1(manifest) => &manifest.package.name,
        }
    }

    pub fn package_path(&self) -> PackagePath {
        match &self.0 {
            VersionedPackageManifest::Version1(manifest) => PackagePath::from_name_and_variant(
                manifest.package.name.to_owned(),
                manifest.package.version.to_owned(),
            ),
        }
    }

    pub fn from_package(package: Package) -> Result<Self, PackageManifestError> {
        let mut blobs = Vec::with_capacity(package.blobs().len());
        for (merkle, blob_entry) in package.blobs().iter() {
            blobs.push(BlobInfo {
                source_path: blob_entry.source_path().into_os_string().into_string().map_err(
                    |source_path| PackageManifestError::InvalidBlobPath {
                        merkle: *merkle,
                        source_path,
                    },
                )?,
                path: blob_entry.blob_path().to_string(),
                merkle: *merkle,
                size: blob_entry.size(),
            })
        }
        let package_metadata = PackageMetadata {
            name: package.meta_package().name().to_owned(),
            version: package.meta_package().variant().to_owned(),
        };
        let manifest_v1 = PackageManifestV1 {
            package: package_metadata,
            blobs,
            repository: None,
            blob_sources_relative: Default::default(),
        };
        Ok(PackageManifest(VersionedPackageManifest::Version1(manifest_v1)))
    }
}

pub struct PackageManifestBuilder {
    manifest: PackageManifestV1,
}

impl PackageManifestBuilder {
    pub fn new(meta_package: MetaPackage) -> Self {
        Self {
            manifest: PackageManifestV1 {
                package: PackageMetadata {
                    name: meta_package.name().to_owned(),
                    version: meta_package.variant().to_owned(),
                },
                blobs: vec![],
                repository: None,
                blob_sources_relative: Default::default(),
            },
        }
    }

    pub fn repository(mut self, repository: impl Into<String>) -> Self {
        self.manifest.repository = Some(repository.into());
        self
    }

    pub fn add_blob(mut self, info: BlobInfo) -> Self {
        self.manifest.blobs.push(info);
        self
    }

    pub fn build(self) -> PackageManifest {
        PackageManifest(VersionedPackageManifest::Version1(self.manifest))
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(tag = "version")]
enum VersionedPackageManifest {
    #[serde(rename = "1")]
    Version1(PackageManifestV1),
}

impl From<PackageManifestV1> for PackageManifest {
    fn from(manifest: PackageManifestV1) -> Self {
        PackageManifest(VersionedPackageManifest::Version1(manifest))
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
struct PackageManifestV1 {
    package: PackageMetadata,
    blobs: Vec<BlobInfo>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    repository: Option<String>,

    /// Are the blob source_paths relative to the working dir (default, as made
    /// by 'pm') or the file containing the serialized manifest (new, portable,
    /// behavior)
    #[serde(default, skip_serializing_if = "RelativeTo::is_default")]
    blob_sources_relative: RelativeTo,
}

/// If the path is a relative path, what is it relative from?
///
/// If 'RelativeTo::WorkingDir', then the path is assumed to be relative to the
/// working dir, and can be used directly as a path.
///
/// If 'RelativeTo::File', then the path is relative to the file that contained
/// the path.  To use the path, it must be resolved against the path to the
/// file.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub enum RelativeTo {
    #[serde(rename = "working_dir")]
    WorkingDir,
    #[serde(rename = "file")]
    File,
}

impl Default for RelativeTo {
    fn default() -> Self {
        RelativeTo::WorkingDir
    }
}

impl RelativeTo {
    pub(crate) fn is_default(&self) -> bool {
        matches!(self, RelativeTo::WorkingDir)
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
struct PackageMetadata {
    name: PackageName,
    version: PackageVariant,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct BlobInfo {
    pub source_path: String,
    pub path: String,
    pub merkle: fuchsia_merkle::Hash,
    pub size: u64,
}

#[cfg(not(target_os = "fuchsia"))]
pub mod host {
    use super::*;
    use crate::PathToStringExt;
    use anyhow::Context;
    use assembly_util::{path_relative_from_file, resolve_path_from_file};
    use std::fs::File;
    use std::io::BufReader;
    use std::path::Path;

    impl PackageManifest {
        pub fn try_load_from(path: impl AsRef<Path>) -> anyhow::Result<Self> {
            let manifest_path = path.as_ref();
            let manifest_str = manifest_path.path_to_string()?;
            let file = File::open(manifest_path)
                .context(format!("Opening package manifest: {}", &manifest_str))?;
            let manifest: Self = serde_json::from_reader(BufReader::new(file))
                .context(format!("Reading package manifest: {}", &manifest_str))?;
            match manifest.0 {
                VersionedPackageManifest::Version1(manifest) => {
                    Ok(manifest.resolve_blob_source_paths(manifest_path)?.into())
                }
            }
        }

        pub fn write_with_relative_blob_paths(
            self,
            path: impl AsRef<Path>,
        ) -> anyhow::Result<Self> {
            match self.0 {
                VersionedPackageManifest::Version1(manifest) => {
                    manifest.write_with_relative_blob_paths(path).map(Into::into)
                }
            }
        }
    }

    impl PackageManifestV1 {
        pub fn write_with_relative_blob_paths(
            self,
            manifest_path: impl AsRef<Path>,
        ) -> anyhow::Result<Self> {
            let manifest = if let RelativeTo::WorkingDir = &self.blob_sources_relative {
                // manifest contains working-dir relative source paths, make
                // them relative to the file, instead.
                let blobs = self
                    .blobs
                    .into_iter()
                    .map(|blob| relativize_blob_source_path(blob, &manifest_path))
                    .collect::<anyhow::Result<Vec<BlobInfo>>>()?;
                Self { blobs, blob_sources_relative: RelativeTo::File, ..self }
            } else {
                self
            };
            let versioned_manifest = VersionedPackageManifest::Version1(manifest.clone());
            let file = File::create(manifest_path)?;
            serde_json::to_writer(file, &versioned_manifest)?;
            Ok(manifest)
        }
    }

    impl PackageManifestV1 {
        pub fn resolve_blob_source_paths(
            self,
            manifest_path: impl AsRef<Path>,
        ) -> anyhow::Result<Self> {
            if let RelativeTo::File = &self.blob_sources_relative {
                let blobs = self
                    .blobs
                    .into_iter()
                    .map(|blob| resolve_blob_source_path(blob, &manifest_path))
                    .collect::<anyhow::Result<Vec<BlobInfo>>>()?;
                Ok(Self { blobs, ..self })
            } else {
                Ok(self)
            }
        }
    }

    fn relativize_blob_source_path(
        blob: BlobInfo,
        manifest_path: impl AsRef<Path>,
    ) -> anyhow::Result<BlobInfo> {
        let source_path = path_relative_from_file(blob.source_path, manifest_path)?;
        let source_path = source_path.path_to_string().with_context(|| {
            format!(
                "Path from UTF-8 string, made relative, is no longer utf-8: {}",
                source_path.display()
            )
        })?;

        Ok(BlobInfo { source_path, ..blob })
    }

    fn resolve_blob_source_path(
        blob: BlobInfo,
        manifest_path: impl AsRef<Path>,
    ) -> anyhow::Result<BlobInfo> {
        let source_path = resolve_path_from_file(&blob.source_path, manifest_path)?
            .path_to_string()
            .context(format!("Resolving blob path: {}", &blob.source_path))?;
        Ok(BlobInfo { source_path, ..blob })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_merkle::Hash;
    use serde_json::json;
    use std::path::PathBuf;
    use std::str::FromStr;

    #[test]
    fn test_version1_serialization() {
        let manifest = PackageManifest(VersionedPackageManifest::Version1(PackageManifestV1 {
            package: PackageMetadata {
                name: "example".parse().unwrap(),
                version: "0".parse().unwrap(),
            },
            blobs: vec![BlobInfo {
                source_path: "../p1".into(),
                path: "data/p1".into(),
                merkle: "0000000000000000000000000000000000000000000000000000000000000000"
                    .parse()
                    .unwrap(),
                size: 1,
            }],
            repository: None,
            blob_sources_relative: Default::default(),
        }));

        assert_eq!(
            serde_json::to_value(&manifest).unwrap(),
            json!(
                {
                    "version": "1",
                    "package": {
                        "name": "example",
                        "version": "0"
                    },
                    "blobs": [
                        {
                            "source_path": "../p1",
                            "path": "data/p1",
                            "merkle": "0000000000000000000000000000000000000000000000000000000000000000",
                            "size": 1
                        },
                    ]
                }
            )
        );

        let manifest = PackageManifest(VersionedPackageManifest::Version1(PackageManifestV1 {
            package: PackageMetadata {
                name: "example".parse().unwrap(),
                version: "0".parse().unwrap(),
            },
            blobs: vec![BlobInfo {
                source_path: "../p1".into(),
                path: "data/p1".into(),
                merkle: "0000000000000000000000000000000000000000000000000000000000000000"
                    .parse()
                    .unwrap(),
                size: 1,
            }],
            repository: Some("testrepository.org".into()),
            blob_sources_relative: RelativeTo::File,
        }));

        assert_eq!(
            serde_json::to_value(&manifest).unwrap(),
            json!(
                {
                    "version": "1",
                    "repository": "testrepository.org",
                    "package": {
                        "name": "example",
                        "version": "0"
                    },
                    "blobs": [
                        {
                            "source_path": "../p1",
                            "path": "data/p1",
                            "merkle": "0000000000000000000000000000000000000000000000000000000000000000",
                            "size": 1
                        },
                    ],
                    "blob_sources_relative": "file"
                }
            )
        );
    }

    #[test]
    fn test_version1_deserialization() {
        let manifest = serde_json::from_value::<PackageManifest>(json!(
            {
                "version": "1",
                "repository": "testrepository.org",
                "package": {
                    "name": "example",
                    "version": "0"
                },
                "blobs": [
                    {
                        "source_path": "../p1",
                        "path": "data/p1",
                        "merkle": "0000000000000000000000000000000000000000000000000000000000000000",
                        "size": 1
                    },
                ]
            }
        )).expect("valid json");

        assert_eq!(
            manifest,
            PackageManifest(VersionedPackageManifest::Version1(PackageManifestV1 {
                package: PackageMetadata {
                    name: "example".parse().unwrap(),
                    version: "0".parse().unwrap(),
                },
                blobs: vec![BlobInfo {
                    source_path: "../p1".into(),
                    path: "data/p1".into(),
                    merkle: "0000000000000000000000000000000000000000000000000000000000000000"
                        .parse()
                        .unwrap(),
                    size: 1
                }],
                repository: Some("testrepository.org".into()),
                blob_sources_relative: Default::default(),
            }))
        );

        let manifest = serde_json::from_value::<PackageManifest>(json!(
            {
                "version": "1",
                "package": {
                    "name": "example",
                    "version": "0"
                },
                "blobs": [
                    {
                        "source_path": "../p1",
                        "path": "data/p1",
                        "merkle": "0000000000000000000000000000000000000000000000000000000000000000",
                        "size": 1
                    },
                ],
                "blob_sources_relative": "file"
            }
        )).expect("valid json");

        assert_eq!(
            manifest,
            PackageManifest(VersionedPackageManifest::Version1(PackageManifestV1 {
                package: PackageMetadata {
                    name: "example".parse().unwrap(),
                    version: "0".parse().unwrap(),
                },
                blobs: vec![BlobInfo {
                    source_path: "../p1".into(),
                    path: "data/p1".into(),
                    merkle: "0000000000000000000000000000000000000000000000000000000000000000"
                        .parse()
                        .unwrap(),
                    size: 1
                }],
                repository: None,
                blob_sources_relative: RelativeTo::File,
            }))
        )
    }

    #[test]
    fn test_create_package_manifest_from_package() {
        let mut package_builder = Package::builder("package-name".parse().unwrap());
        package_builder.add_entry(
            String::from("bin/my_prog"),
            Hash::from_str("0000000000000000000000000000000000000000000000000000000000000000")
                .unwrap(),
            PathBuf::from("src/bin/my_prog"),
            1,
        );
        let package = package_builder.build().unwrap();
        let package_manifest = PackageManifest::from_package(package).unwrap();
        assert_eq!(&"package-name".parse::<PackageName>().unwrap(), package_manifest.name());
    }
}

#[cfg(all(test, not(target_os = "fuchsia")))]
mod host_tests {
    use super::*;
    use crate::PathToStringExt;
    use serde_json::Value;
    use std::fs::File;
    use tempfile::TempDir;

    #[test]
    fn test_load_from_simple() {
        let temp_dir = TempDir::new().unwrap();

        let data_dir = temp_dir.path().join("data_source");
        let manifest_dir = temp_dir.path().join("manifest_dir");
        let manifest_path = manifest_dir.join("package_manifest.json");
        let expected_blob_source_path = data_dir.join("p1").path_to_string().unwrap();

        std::fs::create_dir_all(&data_dir).unwrap();
        std::fs::create_dir_all(&manifest_dir).unwrap();

        let manifest = PackageManifest(VersionedPackageManifest::Version1(PackageManifestV1 {
            package: PackageMetadata {
                name: "example".parse().unwrap(),
                version: "0".parse().unwrap(),
            },
            blobs: vec![BlobInfo {
                source_path: expected_blob_source_path.clone(),
                path: "data/p1".into(),
                merkle: "0000000000000000000000000000000000000000000000000000000000000000"
                    .parse()
                    .unwrap(),
                size: 1,
            }],
            repository: None,
            blob_sources_relative: RelativeTo::WorkingDir,
        }));

        let manifest_file = File::create(&manifest_path).unwrap();
        serde_json::to_writer(manifest_file, &manifest).unwrap();

        let loaded_manifest = PackageManifest::try_load_from(&manifest_path).unwrap();
        assert_eq!(loaded_manifest.name(), &"example".parse::<PackageName>().unwrap());

        let blobs = loaded_manifest.into_blobs();
        assert_eq!(blobs.len(), 1);
        let blob = blobs.first().unwrap();
        assert_eq!(blob.path, "data/p1");
        assert_eq!(blob.source_path, expected_blob_source_path);
    }

    #[test]
    fn test_load_from_resolves_source_paths() {
        let temp_dir = TempDir::new().unwrap();

        let data_dir = temp_dir.path().join("data_source");
        let manifest_dir = temp_dir.path().join("manifest_dir");
        let manifest_path = manifest_dir.join("package_manifest.json");
        let expected_blob_source_path = data_dir.join("p1").path_to_string().unwrap();

        std::fs::create_dir_all(&data_dir).unwrap();
        std::fs::create_dir_all(&manifest_dir).unwrap();

        let manifest = PackageManifest(VersionedPackageManifest::Version1(PackageManifestV1 {
            package: PackageMetadata {
                name: "example".parse().unwrap(),
                version: "0".parse().unwrap(),
            },
            blobs: vec![BlobInfo {
                source_path: "../data_source/p1".into(),
                path: "data/p1".into(),
                merkle: "0000000000000000000000000000000000000000000000000000000000000000"
                    .parse()
                    .unwrap(),
                size: 1,
            }],
            repository: None,
            blob_sources_relative: RelativeTo::File,
        }));

        let manifest_file = File::create(&manifest_path).unwrap();
        serde_json::to_writer(manifest_file, &manifest).unwrap();

        let loaded_manifest = PackageManifest::try_load_from(&manifest_path).unwrap();
        assert_eq!(loaded_manifest.name(), &"example".parse::<PackageName>().unwrap());

        let blobs = loaded_manifest.into_blobs();
        assert_eq!(blobs.len(), 1);
        let blob = blobs.first().unwrap();
        assert_eq!(blob.path, "data/p1");
        assert_eq!(blob.source_path, expected_blob_source_path);
    }

    #[test]
    fn test_write_package_manifest_already_relative() {
        let temp_dir = TempDir::new().unwrap();

        let data_dir = temp_dir.path().join("data_source");
        let manifest_dir = temp_dir.path().join("manifest_dir");
        let manifest_path = manifest_dir.join("package_manifest.json");

        std::fs::create_dir_all(&data_dir).unwrap();
        std::fs::create_dir_all(&manifest_dir).unwrap();

        let manifest = PackageManifest(VersionedPackageManifest::Version1(PackageManifestV1 {
            package: PackageMetadata {
                name: "example".parse().unwrap(),
                version: "0".parse().unwrap(),
            },
            blobs: vec![BlobInfo {
                source_path: "../data_source/p1".into(),
                path: "data/p1".into(),
                merkle: "0000000000000000000000000000000000000000000000000000000000000000"
                    .parse()
                    .unwrap(),
                size: 1,
            }],
            repository: None,
            blob_sources_relative: RelativeTo::File,
        }));

        let result_manifest =
            manifest.clone().write_with_relative_blob_paths(&manifest_path).unwrap();

        // The manifest should not have been changed in this case.
        assert_eq!(result_manifest, manifest);

        let parsed_manifest: Value =
            serde_json::from_reader(File::open(manifest_path).unwrap()).unwrap();
        let object = parsed_manifest.as_object().unwrap();
        let version = object.get("version").unwrap();
        let blobs_value = object.get("blobs").unwrap();
        let blobs = blobs_value.as_array().unwrap();
        let blob_value = blobs.first().unwrap();
        let blob = blob_value.as_object().unwrap();
        let source_path_value = blob.get("source_path").unwrap();
        let source_path = source_path_value.as_str().unwrap();

        assert_eq!(version, "1");
        assert_eq!(source_path, "../data_source/p1");
    }

    #[test]
    fn test_write_package_manifest_making_paths_relative() {
        let temp_dir = TempDir::new().unwrap();

        let data_dir = temp_dir.path().join("data_source");
        let manifest_dir = temp_dir.path().join("manifest_dir");
        let manifest_path = manifest_dir.join("package_manifest.json");
        let blob_source_path = data_dir.join("p2").path_to_string().unwrap();

        std::fs::create_dir_all(&data_dir).unwrap();
        std::fs::create_dir_all(&manifest_dir).unwrap();

        let manifest = PackageManifest(VersionedPackageManifest::Version1(PackageManifestV1 {
            package: PackageMetadata {
                name: "example".parse().unwrap(),
                version: "0".parse().unwrap(),
            },
            blobs: vec![BlobInfo {
                source_path: blob_source_path,
                path: "data/p2".into(),
                merkle: "0000000000000000000000000000000000000000000000000000000000000000"
                    .parse()
                    .unwrap(),
                size: 1,
            }],
            repository: None,
            blob_sources_relative: RelativeTo::WorkingDir,
        }));

        let result_manifest =
            manifest.clone().write_with_relative_blob_paths(&manifest_path).unwrap();
        let blob = result_manifest.blobs().first().unwrap();
        assert_eq!(blob.source_path, "../data_source/p2");

        let parsed_manifest: serde_json::Value =
            serde_json::from_reader(File::open(manifest_path).unwrap()).unwrap();

        let object = parsed_manifest.as_object().unwrap();
        let blobs_value = object.get("blobs").unwrap();
        let blobs = blobs_value.as_array().unwrap();
        let blob_value = blobs.first().unwrap();
        let blob = blob_value.as_object().unwrap();
        let source_path_value = blob.get("source_path").unwrap();
        let source_path = source_path_value.as_str().unwrap();

        assert_eq!(source_path, "../data_source/p2");
    }
}
