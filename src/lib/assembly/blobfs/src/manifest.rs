// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use fuchsia_hash::Hash;
use fuchsia_merkle::MerkleTree;
use fuchsia_pkg::PackageManifest;
use pathdiff::diff_paths;
use std::collections::BTreeMap;
use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};

/// A list of mappings between blob merkle and path on the host.
#[derive(Default, Debug)]
pub struct BlobManifest {
    /// Map between file merkle and path on the host.
    /// The path is relative to the working directory.
    packages: BTreeMap<Hash, PathBuf>,
}

impl BlobManifest {
    /// Add all the files from the `package` on the host.
    pub fn add_package(&mut self, package: PackageManifest) -> Result<()> {
        for blob in package.into_blobs() {
            self.add_file_with_merkle(blob.source_path, blob.merkle);
        }
        Ok(())
    }

    /// Add a file from the host at `path`.
    pub fn add_file(&mut self, path: impl AsRef<Path>) -> Result<()> {
        let file = File::open(&path)?;
        let merkle = MerkleTree::from_reader(&file)
            .context(format!(
                "Failed to calculate the merkle for file: {}",
                &path.as_ref().display()
            ))?
            .root();
        self.add_file_with_merkle(path, merkle);
        Ok(())
    }

    /// Add a file from the host at `path` with `merkle`.
    fn add_file_with_merkle(&mut self, path: impl AsRef<Path>, merkle: Hash) {
        self.packages.insert(merkle, path.as_ref().to_path_buf());
    }

    /// Generate the manifest of blobs to insert into BlobFS and write it to
    /// `path`. The blob paths inside the manifest are relative to the manifest
    /// itself, therefore we must pass the path to this function. The format of
    /// the output is:
    ///
    /// e9d5e=path/on/host/to/meta.far
    /// 38203=path/on/host/to/file.json
    ///
    pub fn write(&self, path: impl AsRef<Path>) -> Result<()> {
        let manifest_parent = path
            .as_ref()
            .parent()
            .ok_or(anyhow!("Failed to get parent path of the output blob manifest"))?;
        let mut out = File::create(&path)?;
        for (merkle, blob_path) in self.packages.iter() {
            let blob_path = path_relative_to_dir(&blob_path, &manifest_parent).context(format!(
                "Failed to get relative path for blob: {}",
                blob_path.display()
            ))?;
            let blob_path = blob_path
                .to_str()
                .context(format!("File path is not valid UTF-8: {}", blob_path.display()))?;
            writeln!(out, "{}={}", merkle, blob_path)?;
        }
        Ok(())
    }
}

/// Rebase |path| onto |dir| even if |dir| is not in the current working directory.
fn path_relative_to_dir(path: impl AsRef<Path>, dir: impl AsRef<Path>) -> Result<PathBuf> {
    // Get the canonical paths for the inputs, so that they can be rebased even if they are in
    // different directories.
    let path = path
        .as_ref()
        .canonicalize()
        .context(format!("Failed to get canonical path for {}", path.as_ref().display()))?;
    let dir = dir
        .as_ref()
        .canonicalize()
        .context(format!("Failed to get canonical path for {}", dir.as_ref().display()))?;

    // Rebase the paths.
    diff_paths(&path, &dir)
        .ok_or(anyhow!("Failed to get relative path for file: {}", path.display()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use tempfile::{NamedTempFile, TempDir};

    #[test]
    fn blob_manifest_with_file() {
        // Create a test file.
        let mut file = NamedTempFile::new().unwrap();
        write!(file, "Council of Ricks").unwrap();

        // Put the file in the manifest.
        let mut manifest = BlobManifest::default();
        manifest.add_file(file.path()).unwrap();

        // Write the manifest.
        let output = NamedTempFile::new().unwrap();
        manifest.write(output.path()).unwrap();

        // Ensure the contents are correct.
        let filename = file.path().file_name().unwrap().to_string_lossy();
        let expected_str = format!(
            "11062f138c675302fb551276f6a2afda16a43025549ac0c63164f6f1d9253da4={}\n",
            filename
        );
        let output_str = std::fs::read_to_string(output).unwrap();
        assert_eq!(output_str, expected_str);
    }

    #[test]
    fn blob_manifest_with_package() {
        // Create a test file.
        let mut file = NamedTempFile::new().unwrap();
        write!(file, "Council of Ricks").unwrap();

        // Create a test package manifest.
        let package = generate_test_manifest("package", "0", Some(file.path()));

        // Put the package in the manifest.
        let mut manifest = BlobManifest::default();
        manifest.add_package(package).unwrap();

        // Write the manifest.
        let output = NamedTempFile::new().unwrap();
        manifest.write(output.path()).unwrap();

        // Ensure the contents are correct.
        let filename = file.path().file_name().unwrap().to_string_lossy();
        let expected_str = format!(
            "0000000000000000000000000000000000000000000000000000000000000000={}\n",
            filename
        );
        let output_str = std::fs::read_to_string(output).unwrap();
        assert_eq!(output_str, expected_str);
    }

    #[test]
    fn blob_manifest_with_file_and_package() {
        // Create a test file.
        let mut file = NamedTempFile::new().unwrap();
        write!(file, "Council of Ricks").unwrap();

        // Create a test package manifest.
        let package = generate_test_manifest("package", "0", Some(file.path()));

        // Add them both to the manifest.
        let mut manifest = BlobManifest::default();
        manifest.add_file(file.path()).unwrap();
        manifest.add_package(package).unwrap();

        // Write the manifest.
        let output = NamedTempFile::new().unwrap();
        manifest.write(output.path()).unwrap();

        // Ensure the contents are correct.
        let filename = file.path().file_name().unwrap().to_string_lossy();
        let expected_str = format!(
            "0000000000000000000000000000000000000000000000000000000000000000={}\n\
             11062f138c675302fb551276f6a2afda16a43025549ac0c63164f6f1d9253da4={}\n",
            filename, filename
        );
        let output_str = std::fs::read_to_string(output).unwrap();
        assert_eq!(output_str, expected_str);
    }

    #[test]
    fn test_relative_blob_path() {
        // Save the current working directory so that we can reset it later.
        let cwd = std::env::current_dir().unwrap();

        // Set a test working directory.
        let temp_dir = std::env::temp_dir();
        let test_cwd = TempDir::new().unwrap();
        let test_cwd_relative = diff_paths(&test_cwd, &temp_dir).unwrap();
        std::env::set_current_dir(&test_cwd).unwrap();

        // Create a test blob in the test working directory.
        let blob_path = test_cwd.path().join("blob");
        let mut blob = File::create(&blob_path).unwrap();
        write!(blob, "Council of Ricks").unwrap();

        // Create a second test directory to rebase the blob path onto, and calculate its path
        // relative to the test working directory.
        let test_out = TempDir::new().unwrap();
        let test_out_relative = diff_paths(&test_out, &test_cwd_relative).unwrap();

        // Calculate the expected path to the blob relative to the test output directory.
        let blob_path_relative = test_cwd_relative.join("blob");
        let expected = PathBuf::from("..").join(&blob_path_relative);

        // Ensure the output is correct.
        assert_eq!(expected, path_relative_to_dir("blob", test_out_relative).unwrap());

        // Reset the working directory.
        std::env::set_current_dir(cwd).unwrap();
    }

    // Generates a package manifest to be used for testing. The `name` is used in the blob file
    // names to make each manifest somewhat unique. If supplied, `file_path` will be used as the
    // non-meta-far blob source path, which allows the tests to use a real file.
    // TODO(fxbug.dev/76993): See if we can share this with BasePackage.
    fn generate_test_manifest(
        name: &str,
        version: &str,
        file_path: Option<impl AsRef<Path>>,
    ) -> PackageManifest {
        let file_source = match file_path {
            Some(path) => path.as_ref().to_string_lossy().into_owned(),
            _ => format!("path/to/{}/file.txt", name),
        };
        serde_json::from_value::<PackageManifest>(json!(
            {
                "version": "1",
                "repository": "testrepository.com",
                "package": {
                    "name": name,
                    "version": version,
                },
                "blobs": [
                    {
                        "source_path": file_source,
                        "path": "data/file.txt",
                        "merkle":
                            "0000000000000000000000000000000000000000000000000000000000000000",
                        "size": 1
                    },
                ]
            }
        ))
        .expect("valid json")
    }
}
