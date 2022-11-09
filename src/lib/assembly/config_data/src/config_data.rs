// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use assembly_util::{DuplicateKeyError, InsertUniqueExt, MapEntry};
use camino::{Utf8Path, Utf8PathBuf};
use fuchsia_pkg::{PackageBuilder, RelativeTo};
use std::collections::BTreeMap;

/// The mapping of a config_data entry's file-path in the package's config_data
/// to it's source path on the filesystem.
type FileEntryMap = BTreeMap<Utf8PathBuf, Utf8PathBuf>;

/// A typename to clarify intent around what Strings are package names.
type PackageName = String;

// The config_data entries for each package, by package name
type ConfigDataMap = BTreeMap<PackageName, FileEntryMap>;

/// A builder for the config_data package.
#[derive(Default)]
pub struct ConfigDataBuilder {
    /// A map of the files to put into config_data, by the name of the package
    /// that they are config_data for.
    for_packages: ConfigDataMap,
}

impl ConfigDataBuilder {
    /// Add a file from the filesystem to config_data for a given package, at a
    /// particular path in the package's namespace.
    pub fn add_entry(
        &mut self,
        package_name: &PackageName,
        destination: Utf8PathBuf,
        source: Utf8PathBuf,
    ) -> Result<()> {
        let package_entries = self.for_packages.entry(package_name.clone()).or_default();
        package_entries.try_insert_unique(MapEntry(destination, source)).map_err(|error|
            anyhow!(
                "Found a duplicate config_data entry for package '{}' at path: '{}': '{}' and was already '{}'",
                package_name,
                error.key(),
                error.new_value(),
                error.previous_value()))
    }

    /// Build the config_data package, in the specified outdir, and return the
    /// path to the `config_data` package's manifest.
    pub fn build(self, outdir: impl AsRef<Utf8Path>) -> Result<Utf8PathBuf> {
        let outdir = outdir.as_ref().join("config_data");
        let mut package_builder = PackageBuilder::new("config-data");

        for (package_name, entries) in self.for_packages {
            for (destination_path, source_file) in entries {
                let config_data_package_path =
                    Utf8PathBuf::from("meta/data").join(&package_name).join(destination_path);
                package_builder
                    .add_file_to_far(config_data_package_path, source_file.to_string())?;
            }
        }

        let metafar_path = outdir.join("meta.far");
        let manifest_path = outdir.join("package_manifest.json");

        package_builder.manifest_path(&manifest_path);
        package_builder.manifest_blobs_relative_to(RelativeTo::File);
        package_builder.repository("fuchsia.com");

        package_builder
            .build(outdir, &metafar_path)
            .context(format!("Building `config_data` package at path '{}'", metafar_path))?;

        Ok(manifest_path)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use camino::Utf8Path;
    use fuchsia_pkg::PackageManifest;
    use std::fs::File;
    use tempfile::{NamedTempFile, TempDir};

    #[test]
    fn test_builder() {
        let tmp = TempDir::new().unwrap();
        let outdir = Utf8Path::from_path(tmp.path()).unwrap();

        let config_data_metafar_path = outdir.join("config_data").join("meta.far");

        // Create a file to write to the package.
        let source_file_path = NamedTempFile::new().unwrap();
        std::fs::write(&source_file_path, "some data").unwrap();

        let source_file_path = source_file_path.into_temp_path();

        // Add the file.
        let mut builder = ConfigDataBuilder::default();
        builder
            .add_entry(
                &"foo".to_string(),
                "dest/path".into(),
                Utf8Path::from_path(source_file_path.as_ref()).unwrap().to_owned(),
            )
            .unwrap();

        // Build the package.
        let manifest_path = builder.build(&outdir).unwrap();

        // Read the package manifest back in.
        let config_data_manifest = PackageManifest::try_load_from(manifest_path).unwrap();
        assert_eq!(config_data_manifest.name().as_ref(), "config-data");

        let mut config_data_metafar = File::open(config_data_metafar_path).unwrap();
        let mut far_reader = fuchsia_archive::Utf8Reader::new(&mut config_data_metafar).unwrap();
        let config_file_data = far_reader.read_file("meta/data/foo/dest/path").unwrap();
        assert_eq!(config_file_data, "some data".as_bytes());
    }

    #[test]
    fn test_builder_rejects_duplicates() {
        let mut builder = ConfigDataBuilder::default();
        let _ = builder.add_entry(&"foo".to_string(), "dest/path".into(), "source/path".into());
        builder
            .add_entry(&"foo".to_string(), "dest/path".into(), "source/path".into())
            .unwrap_err();
    }
}
