// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        BlobEntry, MetaContents, MetaPackage, Package, PackageManifestError, PackageName,
        PackagePath, PackageVariant,
    },
    anyhow::Result,
    fuchsia_archive::{self, Utf8Reader},
    fuchsia_hash::Hash,
    fuchsia_merkle::from_slice,
    fuchsia_url::{RepositoryUrl, UnpinnedAbsolutePackageUrl},
    serde::{Deserialize, Serialize},
    std::{
        collections::BTreeMap,
        fs::{self, File},
        io,
        io::{Read, Seek, SeekFrom, Write},
        path::Path,
        str,
    },
};

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
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

    pub async fn archive(
        self,
        root_dir: impl AsRef<Path>,
        out: impl Write,
    ) -> Result<(), PackageManifestError> {
        let root_dir = root_dir.as_ref();

        let mut contents: BTreeMap<_, (_, Box<dyn Read>)> = BTreeMap::new();
        for blob in self.into_blobs() {
            let source_path = root_dir.join(blob.source_path);
            if blob.path == "meta/" {
                let mut meta_far_blob = File::open(&source_path).map_err(|err| {
                    PackageManifestError::IoErrorWithPath { cause: err, path: source_path }
                })?;
                meta_far_blob.seek(SeekFrom::Start(0))?;
                contents.insert(
                    "meta.far".to_string(),
                    (meta_far_blob.metadata()?.len(), Box::new(meta_far_blob)),
                );
            } else {
                let blob_file = File::open(&source_path).map_err(|err| {
                    PackageManifestError::IoErrorWithPath { cause: err, path: source_path }
                })?;
                contents.insert(
                    blob.merkle.to_string(),
                    (blob_file.metadata()?.len(), Box::new(blob_file)),
                );
            }
        }
        fuchsia_archive::write(out, contents)?;
        Ok(())
    }

    pub fn package_path(&self) -> PackagePath {
        match &self.0 {
            VersionedPackageManifest::Version1(manifest) => PackagePath::from_name_and_variant(
                manifest.package.name.to_owned(),
                manifest.package.version.to_owned(),
            ),
        }
    }

    pub fn repository(&self) -> Option<&str> {
        match &self.0 {
            VersionedPackageManifest::Version1(manifest) => manifest.repository.as_deref(),
        }
    }

    pub fn package_url(&self) -> Result<Option<UnpinnedAbsolutePackageUrl>> {
        if let Some(url) = self.repository() {
            let repo = RepositoryUrl::parse_host(url.to_string())?;
            return Ok(Some(UnpinnedAbsolutePackageUrl::new(repo, self.name().clone(), None)));
        };
        Ok(None)
    }

    /// Returns the merkle root of the meta.far.
    ///
    /// # Panics
    ///
    /// Panics if the PackageManifest is missing a "meta/" entry
    pub fn hash(&self) -> Hash {
        self.blobs().iter().find(|blob| blob.path == "meta/").unwrap().merkle
    }

    /// Create a `PackageManifest` from a blobs directory and the meta.far hash.
    ///
    /// This directory must be a flat file that contains all the package blobs.
    pub fn from_blobs_dir(dir: &Path, meta_far_hash: Hash) -> Result<Self, PackageManifestError> {
        let meta_far_path = dir.join(meta_far_hash.to_string());

        let mut meta_far_file = File::open(&meta_far_path)?;
        let meta_far_size = meta_far_file.metadata()?.len();

        let mut meta_far = fuchsia_archive::Utf8Reader::new(&mut meta_far_file)?;

        let meta_contents = meta_far.read_file("meta/contents")?;
        let meta_contents = MetaContents::deserialize(meta_contents.as_slice())?.into_contents();

        // The meta contents are unordered, so sort them to keep things consistent.
        let meta_contents = meta_contents.into_iter().collect::<BTreeMap<_, _>>();

        let meta_package = meta_far.read_file("meta/package")?;
        let meta_package = MetaPackage::deserialize(meta_package.as_slice())?;

        // Build the PackageManifest of this package.
        let mut builder = PackageManifestBuilder::new(meta_package);

        // Add the meta.far blob. We add this first since some scripts assume the first entry is the
        // meta.far entry.
        builder = builder.add_blob(BlobInfo {
            source_path: meta_far_path.into_os_string().into_string().map_err(|source_path| {
                PackageManifestError::InvalidBlobPath {
                    merkle: meta_far_hash,
                    source_path: source_path.into(),
                }
            })?,
            path: "meta/".into(),
            merkle: meta_far_hash,
            size: meta_far_size,
        });

        for (blob_path, merkle) in meta_contents.into_iter() {
            let source_path = dir.join(merkle.to_string()).canonicalize()?;

            if !source_path.exists() {
                return Err(PackageManifestError::IoErrorWithPath {
                    cause: io::ErrorKind::NotFound.into(),
                    path: source_path,
                });
            }

            let size = fs::metadata(&source_path)?.len();

            builder = builder.add_blob(BlobInfo {
                source_path: source_path.into_os_string().into_string().map_err(|source_path| {
                    PackageManifestError::InvalidBlobPath {
                        merkle,
                        source_path: source_path.into(),
                    }
                })?,
                path: blob_path,
                merkle,
                size,
            });
        }

        Ok(builder.build())
    }

    /// Extract the package blobs from `archive_path` into the `out_dir` directory and
    /// returns a `PackageManifest` for these files.
    pub fn from_archive(archive_path: &Path, out_dir: &Path) -> Result<Self, PackageManifestError> {
        let archive_file = File::open(archive_path)?;
        let mut archive_reader = Utf8Reader::new(&archive_file)?;
        let meta_far = archive_reader.read_file("meta.far")?;
        let meta_far_hash = from_slice(&meta_far[..]).root();

        let output_meta_far_read = std::io::Cursor::new(&meta_far);
        let mut meta_far_reader = Utf8Reader::new(output_meta_far_read)?;
        let meta_contents = meta_far_reader.read_file("meta/contents")?;
        let file_list = MetaContents::deserialize(meta_contents.as_slice())?.into_contents();

        let meta_far_path = out_dir.join(meta_far_hash.to_string());
        std::fs::write(meta_far_path, &meta_far)?;

        for (file, hash) in file_list {
            let hash = hash.to_string();
            let contents = match archive_reader.read_file(&hash) {
                Ok(contents) => contents,
                Err(fuchsia_archive::Error::PathNotPresent(_)) => {
                    archive_reader.read_file(&file)?
                }
                Err(err) => {
                    return Err(err.into());
                }
            };
            std::fs::write(out_dir.join(hash), contents)?;
        }
        PackageManifest::from_blobs_dir(out_dir, meta_far_hash)
    }

    pub fn from_package(
        package: Package,
        repository: Option<String>,
    ) -> Result<Self, PackageManifestError> {
        let mut blobs = Vec::with_capacity(package.blobs().len());

        let mut push_blob = |blob_path, blob_entry: BlobEntry| {
            let source_path = blob_entry.source_path();

            blobs.push(BlobInfo {
                source_path: source_path.into_os_string().into_string().map_err(|source_path| {
                    PackageManifestError::InvalidBlobPath {
                        merkle: blob_entry.hash(),
                        source_path: source_path.into(),
                    }
                })?,
                path: blob_path,
                merkle: blob_entry.hash(),
                size: blob_entry.size(),
            });

            Ok::<(), PackageManifestError>(())
        };

        let mut package_blobs = package.blobs();

        // Add the meta.far blob. We add this first since some scripts assume the first entry is the
        // meta.far entry.
        if let Some((blob_path, blob_entry)) = package_blobs.remove_entry("meta/") {
            push_blob(blob_path, blob_entry)?;
        }

        for (blob_path, blob_entry) in package_blobs {
            push_blob(blob_path, blob_entry)?;
        }

        let manifest_v1 = PackageManifestV1 {
            package: PackageMetadata {
                name: package.meta_package().name().to_owned(),
                version: package.meta_package().variant().to_owned(),
            },
            blobs,
            repository,
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

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
struct PackageManifestV1 {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    repository: Option<String>,
    package: PackageMetadata,
    blobs: Vec<BlobInfo>,

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

pub mod host {
    use super::*;
    use anyhow::Context;
    use assembly_util::{path_relative_from_file, resolve_path_from_file};
    use camino::Utf8Path;
    use std::fs::File;

    impl PackageManifest {
        pub fn try_load_from(manifest_path: impl AsRef<Utf8Path>) -> anyhow::Result<Self> {
            fn inner(manifest_path: &Utf8Path) -> anyhow::Result<PackageManifest> {
                let file = File::open(manifest_path)
                    .with_context(|| format!("Opening package manifest: {}", manifest_path))?;

                PackageManifest::from_reader(manifest_path, file)
            }
            inner(manifest_path.as_ref())
        }

        pub fn from_reader(
            manifest_path: impl AsRef<Utf8Path>,
            reader: impl std::io::Read,
        ) -> anyhow::Result<Self> {
            fn inner(
                manifest_path: &Utf8Path,
                reader: impl std::io::Read,
            ) -> anyhow::Result<PackageManifest> {
                let versioned: VersionedPackageManifest = serde_json::from_reader(reader)?;

                let versioned = match versioned {
                    VersionedPackageManifest::Version1(manifest) => {
                        VersionedPackageManifest::Version1(
                            manifest.resolve_blob_source_paths(manifest_path)?,
                        )
                    }
                };

                Ok(Self(versioned))
            }
            inner(manifest_path.as_ref(), reader)
        }

        pub fn write_with_relative_blob_paths(
            self,
            path: impl AsRef<Utf8Path>,
        ) -> anyhow::Result<Self> {
            fn inner(this: PackageManifest, path: &Utf8Path) -> anyhow::Result<PackageManifest> {
                let versioned = match this.0 {
                    VersionedPackageManifest::Version1(manifest) => {
                        VersionedPackageManifest::Version1(
                            manifest.write_with_relative_blob_paths(path)?,
                        )
                    }
                };

                Ok(PackageManifest(versioned))
            }
            inner(self, path.as_ref())
        }
    }

    impl PackageManifestV1 {
        pub fn write_with_relative_blob_paths(
            self,
            manifest_path: impl AsRef<Utf8Path>,
        ) -> anyhow::Result<PackageManifestV1> {
            fn inner(
                this: PackageManifestV1,
                manifest_path: &Utf8Path,
            ) -> anyhow::Result<PackageManifestV1> {
                let manifest = if let RelativeTo::WorkingDir = &this.blob_sources_relative {
                    // manifest contains working-dir relative source paths, make
                    // them relative to the file, instead.
                    let blobs = this
                        .blobs
                        .into_iter()
                        .map(|blob| relativize_blob_source_path(blob, manifest_path))
                        .collect::<anyhow::Result<_>>()?;
                    PackageManifestV1 { blobs, blob_sources_relative: RelativeTo::File, ..this }
                } else {
                    this
                };

                let versioned_manifest = VersionedPackageManifest::Version1(manifest.clone());

                let file = File::create(manifest_path)?;
                serde_json::to_writer(file, &versioned_manifest)?;

                Ok(manifest)
            }
            inner(self, manifest_path.as_ref())
        }
    }

    impl PackageManifestV1 {
        pub fn resolve_blob_source_paths(
            self,
            manifest_path: impl AsRef<Utf8Path>,
        ) -> anyhow::Result<Self> {
            fn inner(
                this: PackageManifestV1,
                manifest_path: &Utf8Path,
            ) -> anyhow::Result<PackageManifestV1> {
                if let RelativeTo::File = &this.blob_sources_relative {
                    let blobs = this
                        .blobs
                        .into_iter()
                        .map(|blob| resolve_blob_source_path(blob, manifest_path))
                        .collect::<anyhow::Result<_>>()?;
                    Ok(PackageManifestV1 { blobs, ..this })
                } else {
                    Ok(this)
                }
            }
            inner(self, manifest_path.as_ref())
        }
    }

    fn relativize_blob_source_path(
        blob: BlobInfo,
        manifest_path: &Utf8Path,
    ) -> anyhow::Result<BlobInfo> {
        let source_path = path_relative_from_file(blob.source_path, manifest_path)?;
        let source_path = source_path.into_string();

        Ok(BlobInfo { source_path, ..blob })
    }

    fn resolve_blob_source_path(
        blob: BlobInfo,
        manifest_path: &Utf8Path,
    ) -> anyhow::Result<BlobInfo> {
        let source_path = resolve_path_from_file(&blob.source_path, manifest_path)
            .with_context(|| format!("Resolving blob path: {}", blob.source_path))?
            .into_string();
        Ok(BlobInfo { source_path, ..blob })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::PackageBuildManifest,
        fuchsia_merkle::Hash,
        pretty_assertions::assert_eq,
        serde_json::json,
        std::{path::PathBuf, str::FromStr},
        tempfile::TempDir,
    };

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
        let manifest = serde_json::from_value::<VersionedPackageManifest>(json!(
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
            VersionedPackageManifest::Version1(PackageManifestV1 {
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
            })
        );

        let manifest = serde_json::from_value::<VersionedPackageManifest>(json!(
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
            VersionedPackageManifest::Version1(PackageManifestV1 {
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
            })
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
        let package_manifest = PackageManifest::from_package(package, None).unwrap();
        assert_eq!(&"package-name".parse::<PackageName>().unwrap(), package_manifest.name());
        assert_eq!(None, package_manifest.repository());
    }

    #[test]
    fn test_from_blobs_dir() {
        let temp = TempDir::new().unwrap();
        let gen_dir = temp.path().join("gen");
        std::fs::create_dir_all(&gen_dir).unwrap();

        let blobs_dir = temp.path().join("blobs");
        std::fs::create_dir_all(&blobs_dir).unwrap();

        // Helper to write some content into a blob.
        let write_blob = |contents| {
            let mut builder = fuchsia_merkle::MerkleTreeBuilder::new();
            builder.write(contents);
            let hash = builder.finish().root();

            let path = blobs_dir.join(hash.to_string());
            std::fs::write(&path, contents).unwrap();

            (path.to_str().unwrap().to_string(), hash)
        };

        // Create a package.
        let (file1_path, file1_hash) = write_blob(b"file 1");
        let (file2_path, file2_hash) = write_blob(b"file 2");

        std::fs::create_dir_all(gen_dir.join("meta")).unwrap();
        let meta_package_path = gen_dir.join("meta").join("package");
        std::fs::write(&meta_package_path, "{\"name\":\"package\",\"version\":\"0\"}").unwrap();

        let external_contents = BTreeMap::from([
            ("file-1".into(), file1_path.clone()),
            ("file-2".into(), file2_path.clone()),
        ]);

        let far_contents = BTreeMap::from([(
            "meta/package".into(),
            meta_package_path.to_str().unwrap().to_string(),
        )]);

        let creation_manifest =
            PackageBuildManifest::from_external_and_far_contents(external_contents, far_contents)
                .unwrap();

        let gen_meta_far_path = temp.path().join("meta.far");
        let _package_manifest =
            crate::build::build(&creation_manifest, &gen_meta_far_path, "package", None);

        // Compute the meta.far hash, and copy it into the blobs/ directory.
        let meta_far_bytes = std::fs::read(&gen_meta_far_path).unwrap();
        let mut merkle_builder = fuchsia_merkle::MerkleTreeBuilder::new();
        merkle_builder.write(&meta_far_bytes);
        let meta_far_hash = merkle_builder.finish().root();

        let meta_far_path = blobs_dir.join(meta_far_hash.to_string());
        std::fs::write(&meta_far_path, &meta_far_bytes).unwrap();

        // We should be able to create a manifest from the blob directory that matches the one
        // created by the builder.
        assert_eq!(
            PackageManifest::from_blobs_dir(&blobs_dir, meta_far_hash).unwrap(),
            PackageManifest(VersionedPackageManifest::Version1(PackageManifestV1 {
                package: PackageMetadata {
                    name: "package".parse().unwrap(),
                    version: PackageVariant::zero(),
                },
                blobs: vec![
                    BlobInfo {
                        source_path: meta_far_path.to_str().unwrap().to_string(),
                        path: "meta/".into(),
                        merkle: meta_far_hash,
                        size: 12288,
                    },
                    BlobInfo {
                        source_path: file1_path,
                        path: "file-1".into(),
                        merkle: file1_hash,
                        size: 6,
                    },
                    BlobInfo {
                        source_path: file2_path,
                        path: "file-2".into(),
                        merkle: file2_hash,
                        size: 6,
                    },
                ],
                repository: None,
                blob_sources_relative: RelativeTo::WorkingDir,
            }))
        );
    }
}

#[cfg(all(test, not(target_os = "fuchsia")))]
mod host_tests {
    use super::*;
    use crate::{path_to_string::PathToStringExt, PackageBuilder};
    use camino::Utf8Path;
    use serde_json::Value;
    use std::{collections::HashMap, fs::File};
    use tempfile::{NamedTempFile, TempDir};

    #[test]
    fn test_load_from_simple() {
        let temp = TempDir::new().unwrap();
        let temp_dir = Utf8Path::from_path(temp.path()).unwrap();

        let data_dir = temp_dir.join("data_source");
        let manifest_dir = temp_dir.join("manifest_dir");
        let manifest_path = manifest_dir.join("package_manifest.json");
        let expected_blob_source_path = data_dir.join("p1").to_string();

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
        let temp = TempDir::new().unwrap();
        let temp_dir = Utf8Path::from_path(temp.path()).unwrap();

        let data_dir = temp_dir.join("data_source");
        let manifest_dir = temp_dir.join("manifest_dir");
        let manifest_path = manifest_dir.join("package_manifest.json");
        let expected_blob_source_path = data_dir.join("p1").to_string();

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
        let temp = TempDir::new().unwrap();
        let temp_dir = Utf8Path::from_path(temp.path()).unwrap();

        let data_dir = temp_dir.join("data_source");
        let manifest_dir = temp_dir.join("manifest_dir");
        let manifest_path = manifest_dir.join("package_manifest.json");

        std::fs::create_dir_all(data_dir).unwrap();
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
        let temp = TempDir::new().unwrap();
        let temp_dir = Utf8Path::from_path(temp.path()).unwrap();

        let data_dir = temp_dir.join("data_source");
        let manifest_dir = temp_dir.join("manifest_dir");
        let manifest_path = manifest_dir.join("package_manifest.json");
        let blob_source_path = data_dir.join("p2").to_string();

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

        let result_manifest = manifest.write_with_relative_blob_paths(&manifest_path).unwrap();
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

    #[test]
    fn test_from_package_archive_bogus() {
        let temp = TempDir::new().unwrap();
        let temp_out_dir = temp.into_path();

        let temp_archive = TempDir::new().unwrap();
        let temp_archive_dir = temp_archive.path();

        let result = PackageManifest::from_archive(temp_archive_dir, &temp_out_dir);
        assert!(result.is_err())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_from_package_manifest_archive_manifest() {
        let outdir = TempDir::new().unwrap();
        let metafar_path = outdir.path().join("meta.far");

        // Create a file to write to the package metafar
        let far_source_file_path = NamedTempFile::new_in(&outdir).unwrap();
        std::fs::write(&far_source_file_path, "some data for far").unwrap();

        // Create a file to include as a blob
        let blob_source_file_path = outdir.path().join("some_blob");
        let blob_contents = "some data for blob";
        std::fs::write(&blob_source_file_path, blob_contents).unwrap();

        // Create a file to include as a blob
        let blob_source_file_path2 = outdir.path().join("another_blob");
        let blob_contents = "some data for blob2";
        std::fs::write(&blob_source_file_path2, blob_contents).unwrap();

        // Create the builder
        let mut builder = PackageBuilder::new("some_pkg_name");
        builder
            .add_file_as_blob(
                "some_blob",
                blob_source_file_path.as_path().path_to_string().unwrap(),
            )
            .unwrap();
        builder
            .add_file_as_blob(
                "another_blob",
                blob_source_file_path2.as_path().path_to_string().unwrap(),
            )
            .unwrap();
        builder
            .add_file_to_far(
                "meta/some/file",
                far_source_file_path.path().path_to_string().unwrap(),
            )
            .unwrap();

        // Build the package
        let manifest = builder.build(&outdir, &metafar_path).unwrap();

        let archive_outdir = TempDir::new().unwrap();
        let archive_path = archive_outdir.path().join("test.far");
        let archive_file = File::create(archive_path.clone()).unwrap();
        manifest.clone().archive(&outdir, &archive_file).await.unwrap();

        let result_outdir = TempDir::new().unwrap().into_path();
        let manifest_2 = PackageManifest::from_archive(&archive_path, &result_outdir).unwrap();
        assert_eq!(manifest_2.package_path(), manifest.package_path());

        let manifest1_blobs =
            manifest.blobs().iter().map(|blob| (blob.merkle, blob)).collect::<HashMap<_, _>>();

        let mut manifest2_blobs =
            manifest_2.blobs().iter().map(|blob| (blob.merkle, blob)).collect::<HashMap<_, _>>();

        for (merkle, blob1) in manifest1_blobs {
            let blob2 = manifest2_blobs.remove_entry(&merkle).unwrap().1;
            assert_eq!(
                std::fs::read(&blob1.source_path).unwrap(),
                std::fs::read(&blob2.source_path).unwrap(),
            );
        }

        assert!(manifest2_blobs.is_empty());
    }
}
