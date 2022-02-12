// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{path_to_string::PathToStringExt, CreationManifest, MetaPackage, PackageManifest},
    anyhow::{anyhow, Context, Result},
    std::{
        collections::BTreeMap,
        convert::TryInto,
        path::{Path, PathBuf},
    },
};

const META_PACKAGE_PATH: &str = "meta/package";
const ABI_REVISION_FILE_PATH: &str = "meta/fuchsia.abi/abi-revision";

/// A builder for Fuchsia Packages
pub struct PackageBuilder {
    /// The name of the package being created.
    name: String,

    /// The abi_revision to embed in the package, if any.
    abi_revision: Option<ABIRevision>,

    /// The contents that are to be placed inside the FAR itself, not as
    /// separate blobs.
    far_contents: BTreeMap<String, String>,

    /// The contents that are to be attached to the package as external blobs.
    blobs: BTreeMap<String, String>,

    /// Optional path to serialize the PackageManifest to
    manifest_path: Option<PathBuf>,

    /// Optional (possibly different) name to publish the package under.
    /// This changes the name that's placed in the output package manifest.
    published_name: Option<String>,
}

impl PackageBuilder {
    /// Create a new PackageBuilder.
    pub fn new(name: impl AsRef<str>) -> Self {
        PackageBuilder {
            name: name.as_ref().to_string(),
            abi_revision: None,
            far_contents: BTreeMap::default(),
            blobs: BTreeMap::default(),
            manifest_path: None,
            published_name: None,
        }
    }

    /// Specify a path to write out the json package manifest to.
    pub fn manifest_path(&mut self, manifest_path: impl Into<PathBuf>) {
        self.manifest_path = Some(manifest_path.into())
    }

    fn validate_ok_to_add_at_path(&self, at_path: impl AsRef<str>) -> Result<()> {
        let at_path = at_path.as_ref();
        if at_path == META_PACKAGE_PATH {
            return Err(anyhow!("Cannot add the 'meta/package' file to a package, it will be created by the PackageBuilder"));
        }
        if at_path == ABI_REVISION_FILE_PATH {
            return Err(anyhow!("Cannot add the 'meta/fuchsia.abi/abi-revision' file to a package, it will be created by the PackageBuilder"));
        }

        if self.far_contents.contains_key(at_path) {
            return Err(anyhow!(
                "Package '{}' already contains a file (in the far) at: '{}'",
                self.name,
                at_path
            ));
        }
        if self.blobs.contains_key(at_path) {
            return Err(anyhow!(
                "Package '{}' already contains a file (as a blob) at: '{}'",
                self.name,
                at_path
            ));
        }

        Ok(())
    }

    /// Add a file to the package's far.
    ///
    /// Errors
    ///
    /// Will return an error if the path for the file is already being used.
    /// Will return an error if any special package metadata paths are used.
    pub fn add_file_to_far(
        &mut self,
        at_path: impl AsRef<str>,
        file: impl AsRef<str>,
    ) -> Result<()> {
        let at_path = at_path.as_ref();
        let file = file.as_ref();
        self.validate_ok_to_add_at_path(at_path)?;

        self.far_contents.insert(at_path.to_string(), file.to_string());

        Ok(())
    }

    /// Add a file to the package as a blob itself.
    ///
    /// Errors
    ///
    /// Will return an error if the path for the file is already being used.
    /// Will return an error if any special package metadata paths are used.
    pub fn add_file_as_blob(
        &mut self,
        at_path: impl AsRef<str>,
        file: impl AsRef<str>,
    ) -> Result<()> {
        let at_path = at_path.as_ref();
        let file = file.as_ref();
        self.validate_ok_to_add_at_path(at_path)?;

        self.blobs.insert(at_path.to_string(), file.to_string());

        Ok(())
    }

    /// Write the contents to a file, and add that file as a blob at the given
    /// path within the package.
    pub fn add_contents_as_blob<C: AsRef<[u8]>>(
        &mut self,
        at_path: impl AsRef<str>,
        contents: C,
        gendir: impl AsRef<Path>,
    ) -> Result<()> {
        // Preflight that the file paths are valid before attempting to write.
        self.validate_ok_to_add_at_path(&at_path)?;
        let source_path = Self::write_contents_to_file(gendir, at_path.as_ref(), contents)?;
        self.add_file_as_blob(at_path, source_path.path_to_string()?)
    }

    /// Write the contents to a file, and add that file to the metafar at the
    /// given path within the package.
    pub fn add_contents_to_far<C: AsRef<[u8]>>(
        &mut self,
        at_path: impl AsRef<str>,
        contents: C,
        gendir: impl AsRef<Path>,
    ) -> Result<()> {
        // Preflight that the file paths are valid before attempting to write.
        self.validate_ok_to_add_at_path(&at_path)?;
        let source_path = Self::write_contents_to_file(gendir, at_path.as_ref(), contents)?;
        self.add_file_to_far(at_path, source_path.path_to_string()?)
    }

    /// Helper fn to write the contents to a file, creating the parent dirs as needed when doing so.
    fn write_contents_to_file<C: AsRef<[u8]>>(
        gendir: impl AsRef<Path>,
        file_path: impl AsRef<Path>,
        contents: C,
    ) -> Result<PathBuf> {
        let file_path = gendir.as_ref().join(file_path);
        if let Some(parent_dir) = file_path.parent() {
            std::fs::create_dir_all(parent_dir)
                .context(format!("creating parent directories for {}", file_path.display()))?;
        }
        std::fs::write(&file_path, contents)
            .context(format!("writing contents to file: {}", file_path.display()))?;
        Ok(file_path)
    }

    /// Set the ABI Revision that should be included in the package.
    pub fn abi_revision(&mut self, abi_revision: u64) {
        self.abi_revision = Some(ABIRevision(abi_revision));
    }

    /// Set a different name for the package to be published by (and to be
    /// included in the generated PackageManifest), than the one embedded in the
    /// package itself.
    pub fn published_name(&mut self, published_name: impl AsRef<str>) {
        self.published_name = Some(published_name.as_ref().into());
    }

    /// Build the package, using the specified dir, returning the
    /// PackageManifest.
    ///
    /// If a path for the manifest was specified, the PackageManifest will also
    /// be written to there.
    ///
    /// The `gendir` param is assumed to be a path to folder which is only used
    /// by this package's creation, so this fn does not try to create paths
    /// within it that are unique across different packages.
    pub fn build(
        self,
        gendir: impl AsRef<Path>,
        metafar_path: impl AsRef<Path>,
    ) -> Result<PackageManifest> {
        let PackageBuilder {
            name,
            abi_revision,
            mut far_contents,
            blobs,
            manifest_path,
            published_name,
        } = self;

        far_contents.insert(
            META_PACKAGE_PATH.to_string(),
            create_meta_package_file(gendir.as_ref(), &name)
                .context(format!("Writing the {} file", META_PACKAGE_PATH))?,
        );

        let abi_revision =
            abi_revision.unwrap_or(ABIRevision::from(version_history::LATEST_VERSION));

        let abi_revision_file =
            Self::write_contents_to_file(gendir, ABI_REVISION_FILE_PATH, abi_revision.as_bytes())
                .context(format!("Writing the {} file", ABI_REVISION_FILE_PATH))?;

        far_contents.insert(
            ABI_REVISION_FILE_PATH.to_string(),
            abi_revision_file
                .path_to_string()
                .context(format!("Adding the {} file to the package", ABI_REVISION_FILE_PATH))?,
        );

        let creation_manifest =
            CreationManifest::from_external_and_far_contents(blobs, far_contents)?;

        let package_manifest = crate::build::build(
            &creation_manifest,
            metafar_path.as_ref(),
            published_name.as_ref().unwrap_or(&name),
        )?;

        if let Some(manifest_path) = manifest_path {
            // Write the package manifest to a file.
            let package_manifest_file = std::fs::File::create(&manifest_path).context(format!(
                "Failed to create package manifest: {}",
                manifest_path.display()
            ))?;
            serde_json::ser::to_writer(package_manifest_file, &package_manifest)?;
        }
        Ok(package_manifest)
    }
}

/// Wrapper around the ABIRevision to help serialize it.
#[derive(Debug, Clone, PartialEq, PartialOrd)]
struct ABIRevision(u64);

impl ABIRevision {
    pub fn as_bytes(&self) -> [u8; 8] {
        self.0.to_le_bytes()
    }
}

impl std::ops::Deref for ABIRevision {
    type Target = u64;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl From<&version_history::Version> for ABIRevision {
    fn from(version: &version_history::Version) -> Self {
        ABIRevision(version.abi_revision)
    }
}

/// Construct a meta/package file in `gendir`.
///
/// Returns the path that the file was created at.
fn create_meta_package_file(gendir: impl AsRef<Path>, name: impl Into<String>) -> Result<String> {
    let package_name = name.into();
    let meta_package_path = gendir.as_ref().join(META_PACKAGE_PATH);
    if let Some(parent_dir) = meta_package_path.parent() {
        std::fs::create_dir_all(parent_dir)?;
    }

    let file = std::fs::File::create(&meta_package_path)?;
    let meta_package = MetaPackage::from_name(package_name.try_into()?);
    meta_package.serialize(file)?;
    meta_package_path.path_to_string()
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_merkle::MerkleTreeBuilder;
    use tempfile::{NamedTempFile, TempDir};

    #[test]
    fn test_create_meta_package_file() {
        let gen_dir = TempDir::new().unwrap();
        let name = "some_test_package";
        let meta_package_path = gen_dir.as_ref().join("meta/package");
        let created_path = create_meta_package_file(&gen_dir, name).unwrap();
        assert_eq!(created_path, meta_package_path.path_to_string().unwrap());

        let raw_contents = std::fs::read(meta_package_path).unwrap();
        let meta_package = MetaPackage::deserialize(std::io::Cursor::new(raw_contents)).unwrap();
        assert_eq!(meta_package.name().as_ref(), "some_test_package");
        assert!(meta_package.variant().is_zero());
    }

    #[test]
    fn test_builder() {
        let outdir = TempDir::new().unwrap();
        let metafar_path = outdir.path().join("meta.far");

        // Create a file to write to the package metafar
        let far_source_file_path = NamedTempFile::new_in(&outdir).unwrap();
        std::fs::write(&far_source_file_path, "some data for far").unwrap();

        // Create a file to include as a blob
        let blob_source_file_path = NamedTempFile::new_in(&outdir).unwrap();
        let blob_contents = "some data for blob";
        std::fs::write(&blob_source_file_path, blob_contents).unwrap();

        // Pre-calculate the blob's hash
        let mut merkle_builder = MerkleTreeBuilder::new();
        merkle_builder.write(blob_contents.as_bytes());
        let hash = merkle_builder.finish().root();

        // Create the builder
        let mut builder = PackageBuilder::new("some_pkg_name");
        builder
            .add_file_as_blob("some/blob", blob_source_file_path.path().path_to_string().unwrap())
            .unwrap();
        builder
            .add_file_to_far(
                "meta/some/file",
                far_source_file_path.path().path_to_string().unwrap(),
            )
            .unwrap();

        // Build the package
        let manifest = builder.build(&outdir, &metafar_path).unwrap();

        // Validate the returned manifest
        assert_eq!(manifest.name().as_ref(), "some_pkg_name");

        // Validate that the blob has the correct hash and contents
        let blob_info =
            manifest.into_blobs().iter().find(|info| info.path == "some/blob").unwrap().clone();
        assert_eq!(hash, blob_info.merkle);
        assert_eq!(blob_contents, std::fs::read_to_string(blob_info.source_path).unwrap());

        // Validate that the metafar contains the additional file in meta
        let mut metafar = std::fs::File::open(metafar_path).unwrap();
        let mut far_reader = fuchsia_archive::Reader::new(&mut metafar).unwrap();
        let far_file_data = far_reader.read_file("meta/some/file").unwrap();
        let far_file_data = std::str::from_utf8(far_file_data.as_slice()).unwrap();
        assert_eq!(far_file_data, "some data for far");

        // Validate that the abi_revision was written correctly
        let abi_revision_data = far_reader.read_file("meta/fuchsia.abi/abi-revision").unwrap();
        let abi_revision_data: [u8; 8] = abi_revision_data.try_into().unwrap();
        let abi_revision = u64::from_le_bytes(abi_revision_data);
        assert_eq!(abi_revision, version_history::LATEST_VERSION.abi_revision);
    }

    #[test]
    fn test_build_rejects_meta_package() {
        let mut builder = PackageBuilder::new("some_pkg_name");
        assert!(builder.add_file_to_far("meta/package", "some/src/file").is_err());
        assert!(builder.add_file_as_blob("meta/package", "some/src/file").is_err());
    }

    #[test]
    fn test_build_rejects_abi_revision() {
        let mut builder = PackageBuilder::new("some_pkg_name");
        assert!(builder.add_file_to_far("meta/fuchsia.abi/abi-revision", "some/src/file").is_err());
        assert!(builder
            .add_file_as_blob("meta/fuchsia.abi/abi-revision", "some/src/file")
            .is_err());
    }

    #[test]
    fn test_builder_rejects_path_in_far_when_existing_path_in_far() {
        let mut builder = PackageBuilder::new("some_pkg_name");
        builder.add_file_to_far("some/far/file", "some/src/file").unwrap();
        assert!(builder.add_file_to_far("some/far/file", "some/src/file").is_err());
    }

    #[test]
    fn test_builder_rejects_path_as_blob_when_existing_path_in_far() {
        let mut builder = PackageBuilder::new("some_pkg_name");
        builder.add_file_to_far("some/far/file", "some/src/file").unwrap();
        assert!(builder.add_file_as_blob("some/far/file", "some/src/file").is_err());
    }

    #[test]
    fn test_builder_rejects_path_in_far_when_existing_path_as_blob() {
        let mut builder = PackageBuilder::new("some_pkg_name");
        builder.add_file_as_blob("some/far/file", "some/src/file").unwrap();
        assert!(builder.add_file_to_far("some/far/file", "some/src/file").is_err());
    }

    #[test]
    fn test_builder_rejects_path_in_blob_when_existing_path_as_blob() {
        let mut builder = PackageBuilder::new("some_pkg_name");
        builder.add_file_as_blob("some/far/file", "some/src/file").unwrap();
        assert!(builder.add_file_as_blob("some/far/file", "some/src/file").is_err());
    }
}
