// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::create_meta_package_file;
use anyhow::{anyhow, Context, Result};
use fuchsia_pkg::{CreationManifest, PackageManifest};
use std::{
    collections::BTreeMap,
    fs::File,
    path::{Path, PathBuf},
};

/// A builder for Fuchsia Packages
///
/// TODO: Consider moving to `fuchsia_pkg::builder::PackageBuilder`
pub struct PackageBuilder {
    /// The name of the package being created.
    name: String,

    /// The contents that are to be placed inside the FAR itself, not as
    /// separate blobs.
    far_contents: BTreeMap<String, String>,

    /// The contents that are to be attached to the package as external blobs.
    blobs: BTreeMap<String, String>,

    /// Optional path to serialize the PackageManifest to
    manifest_path: Option<PathBuf>,
}

impl PackageBuilder {
    /// Create a new PackageBuilder.
    pub fn new(name: impl AsRef<str>) -> Self {
        PackageBuilder {
            name: name.as_ref().to_string(),
            far_contents: BTreeMap::default(),
            blobs: BTreeMap::default(),
            manifest_path: None,
        }
    }

    /// Specify a path to write out the json package manifest to.
    pub fn manifest_path(&mut self, manifest_path: impl Into<PathBuf>) {
        self.manifest_path = Some(manifest_path.into())
    }

    fn validate_ok_to_add_at_path(&self, at_path: impl AsRef<str>) -> Result<()> {
        let at_path = at_path.as_ref();
        if at_path == "meta/package" {
            return Err(anyhow!("Cannot add the 'meta/package' file to a package, it will be created by the PackageBuilder"));
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

    /// Build the package, using the specified dir, returning the
    /// PackageManifest.
    ///
    /// If a path for the manifest was specified, the PackageManifest will also
    /// be written to there.
    pub fn build(
        self,
        gendir: impl AsRef<Path>,
        metafar_path: impl AsRef<Path>,
    ) -> Result<PackageManifest> {
        let PackageBuilder { name, mut far_contents, blobs, manifest_path } = self;

        far_contents.insert(
            "meta/package".to_string(),
            create_meta_package_file(gendir.as_ref(), "system_image", "0")
                .context("Writing the ")?,
        );

        let creation_manifest =
            CreationManifest::from_external_and_far_contents(blobs, far_contents)?;

        let package_manifest =
            fuchsia_pkg::build(&creation_manifest, metafar_path.as_ref(), &name)?;

        if let Some(manifest_path) = manifest_path {
            // Write the package manifest to a file.
            let package_manifest_file = File::create(&manifest_path)
                .context("Failed to create base_package_manifest.json")?;
            serde_json::ser::to_writer(package_manifest_file, &package_manifest)?;
        }
        Ok(package_manifest)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::PathToStringExt;
    use fuchsia_merkle::MerkleTreeBuilder;
    use std::fs::File;
    use tempfile::{NamedTempFile, TempDir};

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
        let mut metafar = File::open(metafar_path).unwrap();
        let mut far_reader = fuchsia_archive::Reader::new(&mut metafar).unwrap();
        let far_file_data = far_reader.read_file("meta/some/file").unwrap();
        let far_file_data = std::str::from_utf8(far_file_data.as_slice()).unwrap();
        assert_eq!(far_file_data, "some data for far");
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
