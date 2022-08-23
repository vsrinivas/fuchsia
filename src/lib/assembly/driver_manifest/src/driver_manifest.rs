// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    assembly_config_schema::product_config::DriverDetails,
    fuchsia_pkg::PackageBuilder,
    fuchsia_pkg::PackageManifest,
    serde::{Deserialize, Serialize},
    std::fs::File,
    std::path::{Path, PathBuf},
};

const BASE_DRIVER_MANIFEST_PACKAGE_NAME: &str = "driver-manager-base-config";
const BASE_DRIVER_MANIFEST_PATH: &str = "config/base-driver-manifest.json";

/// A driver manifest fragment.
#[derive(Debug, Default, Serialize, Deserialize)]
pub struct DriverManifest {
    driver_url: String,
}

/// A builder for the driver manifest package.
#[derive(Default)]
pub struct DriverManifestBuilder {
    drivers: Vec<DriverManifest>,
}

impl DriverManifestBuilder {
    /// Add a driver manifest fragment to the driver manifest.
    pub fn add_driver(&mut self, driver_details: DriverDetails, package_url: &str) -> Result<()> {
        let driver_manifests = driver_details
            .components
            .iter()
            .map(|component_path| DriverManifest {
                driver_url: format!("{}#{}", package_url, component_path),
            })
            .collect::<Vec<DriverManifest>>();

        self.drivers.extend(driver_manifests);
        Ok(())
    }

    /// Build the driver manifest package and return the driver manifest path.
    pub fn build_driver_manifest_package(&self, gendir: impl AsRef<Path>) -> Result<PathBuf> {
        // Create a directory for the newly generated package
        let packagedir = gendir.as_ref().join(BASE_DRIVER_MANIFEST_PACKAGE_NAME);
        let manifest_path = packagedir.join(BASE_DRIVER_MANIFEST_PATH);

        let manifest_path_str = manifest_path.to_str().ok_or(anyhow!(format!(
            "driver manifest path is not valid UTF-8: {}",
            manifest_path.display()
        )))?;

        if let Some(parent) = manifest_path.parent() {
            std::fs::create_dir_all(parent).context(format!(
                "Creating parent dir {} for {} in gendir",
                parent.display(),
                manifest_path.display()
            ))?;
        }

        let manifest_file = File::create(&manifest_path)
            .context(format!("Creating the driver manifest file: {}", manifest_path.display()))?;

        serde_json::to_writer(manifest_file, &self.drivers)
            .context(format!("Writing the manifest file {}", manifest_path.display()))?;

        let mut builder = PackageBuilder::new(BASE_DRIVER_MANIFEST_PACKAGE_NAME);

        builder.add_file_as_blob(BASE_DRIVER_MANIFEST_PATH, manifest_path_str).context(format!(
            "Adding blob {} to {}",
            BASE_DRIVER_MANIFEST_PATH, manifest_path_str
        ))?;

        let package_manifest_path = packagedir.join("package_manifest.json");
        let metafar_path = packagedir.join("meta.far");
        builder.manifest_path(package_manifest_path.clone());
        builder
            .build(packagedir, metafar_path.clone())
            .context(format!("Building driver manifest package {}", metafar_path.display()))?;

        Ok(package_manifest_path)
    }

    /// Helper function to determine a base driver's package url
    pub fn get_base_package_url(path: impl AsRef<Path>) -> Result<String> {
        // Load the PackageManifest from the given path
        let manifest = PackageManifest::try_load_from(&path).with_context(|| {
            format!("parsing driver package {} as a package manifest", path.as_ref().display())
        })?;

        let repository = match manifest.repository() {
            Some(x) => x,
            None => "fuchsia.com",
        };

        Ok(format!("fuchsia-pkg://{}/{}", repository, manifest.name()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assembly_test_util::generate_test_manifest;
    use camino::Utf8PathBuf;
    use fuchsia_archive::Utf8Reader;
    use std::fs;
    use std::io::Write;
    use tempfile::TempDir;

    #[test]
    fn build_driver_manifest_package() -> Result<()> {
        let outdir = TempDir::new()?;
        std::fs::create_dir(outdir.path().join("driver"))?;
        let driver_package_manifest_file_path = outdir.path().join("driver/package_manifest.json");
        let mut driver_package_manifest_file = File::create(&driver_package_manifest_file_path)?;
        let package_manifest = generate_test_manifest("base_driver", None);
        serde_json::to_writer(&driver_package_manifest_file, &package_manifest)?;
        driver_package_manifest_file.flush()?;

        let driver_details = DriverDetails {
            package: Utf8PathBuf::from_path_buf(driver_package_manifest_file_path.to_owned())
                .unwrap(),
            components: vec![Utf8PathBuf::from("meta/foobar.cm")],
        };
        let mut driver_manifest_builder = DriverManifestBuilder::default();
        driver_manifest_builder.add_driver(
            driver_details,
            &DriverManifestBuilder::get_base_package_url(driver_package_manifest_file_path)?,
        )?;
        driver_manifest_builder.build_driver_manifest_package(outdir.path())?;

        let manifest_path =
            &outdir.path().join("driver-manager-base-config/config/base-driver-manifest.json");
        let manifest_contents = fs::read_to_string(manifest_path)?;
        assert_eq!(
            "[{\"driver_url\":\"fuchsia-pkg://testrepository.com/base_driver#meta/foobar.cm\"}]",
            manifest_contents
        );

        // Read the output and ensure it contains the right files (and their hashes)
        let path = &outdir.path().join("driver-manager-base-config").join("meta.far");

        let mut far_reader = Utf8Reader::new(File::open(path)?)?;
        let contents = far_reader.read_file("meta/contents").unwrap();
        let contents = std::str::from_utf8(&contents).unwrap();
        let expected_contents = "\
            config/base-driver-manifest.json=01b5eb808f8e751298fad6d21e2b29f65b9029a895566062a5f77150ba76cd77\n"
        .to_string();
        assert_eq!(expected_contents, contents);

        Ok(())
    }
}
