// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::manifest::BlobManifest;
use anyhow::{Context, Result};
use assembly_util::PathToStringExt;
use fuchsia_pkg::PackageManifest;
use std::fs::File;
use std::io::BufReader;
use std::path::Path;

/// Builder for BlobFS.
///
/// Example usage:
///
/// ```
/// let builder = BlobFSBuilder::new();
/// builder.add_package("path/to/package_manifest.json")?;
/// builder.add_file("path/to/file.txt")?;
/// builder.build(gendir, "blob.blk")?;
/// ```
///
pub struct BlobFSBuilder {
    manifest: BlobManifest,
}

impl BlobFSBuilder {
    /// Construct a new BlobFSBuilder.
    pub fn new() -> Self {
        BlobFSBuilder { manifest: BlobManifest::default() }
    }

    /// Add a package to blobfs by inserting every blob mentioned in the
    /// `package_manifest_path` on the host.
    pub fn add_package(&mut self, package_manifest_path: impl AsRef<Path>) -> Result<()> {
        let manifest_file = File::open(package_manifest_path)?;
        let manifest_reader = BufReader::new(manifest_file);
        let manifest: PackageManifest = serde_json::from_reader(manifest_reader)?;
        self.manifest.add_package(manifest)
    }

    /// Add a file to blobfs from the `path` on the host.
    pub fn add_file(&mut self, path: impl AsRef<Path>) -> Result<()> {
        self.manifest.add_file(path)
    }

    /// Build blobfs, and write it to `output`, while placing intermediate files in `gendir`.
    pub fn build(&self, gendir: impl AsRef<Path>, output: impl AsRef<Path>) -> Result<()> {
        // Write the blob manifest.
        let blob_manifest_path = gendir.as_ref().join("blob.manifest");
        self.manifest.write(&blob_manifest_path).context("Failed to write to blob.manifest")?;

        // Build the arguments vector.
        let blobs_json_path = gendir.as_ref().join("blobs.json");
        let blobfs_args = build_blobfs_args(&blob_manifest_path, blobs_json_path, output)?;

        // Run the blobfs tool.
        // TODO(fxbug.dev/76378): Take the tool location from a config.
        let output = std::process::Command::new("host_x64/blobfs").args(&blobfs_args).output();
        let output = output.context("Failed to run the blobfs tool")?;
        if !output.status.success() {
            anyhow::bail!(format!("Failed to generate blobfs with output: {:?}", output));
        }
        Ok(())
    }
}

/// Build the list of arguments to pass to the blobfs tool.
fn build_blobfs_args(
    blob_manifest_path: impl AsRef<Path>,
    blobs_json_path: impl AsRef<Path>,
    output_path: impl AsRef<Path>,
) -> Result<Vec<String>> {
    Ok(vec![
        "--json-output".to_string(),
        blobs_json_path.as_ref().path_to_string()?,
        "--compress".to_string(),
        output_path.as_ref().path_to_string()?,
        "create".to_string(),
        "--manifest".to_string(),
        blob_manifest_path.as_ref().path_to_string()?,
        "--blob_layout_format".to_string(),
        "compact".to_string(),
    ])
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::{NamedTempFile, TempDir};

    #[test]
    fn blobfs_args() {
        let args = build_blobfs_args("blob.manifest", "blobs.json", "blob.blk").unwrap();
        assert_eq!(
            args,
            vec![
                "--json-output",
                "blobs.json",
                "--compress",
                "blob.blk",
                "create",
                "--manifest",
                "blob.manifest",
                "--blob_layout_format",
                "compact",
            ]
        );
    }

    #[test]
    fn blobfs_builder() {
        // Prepare a temporary directory where the intermediate files as well
        // as the input and output files will go.
        let gendir = TempDir::new().unwrap();

        // Create a test file.
        let filepath = gendir.path().join("file.txt");
        let mut file = File::create(&filepath).unwrap();
        write!(file, "Boaty McBoatface").unwrap();

        // Build blobfs.
        let output = NamedTempFile::new().unwrap();
        let mut builder = BlobFSBuilder::new();
        builder.add_file(&filepath).unwrap();
        builder.build(&gendir.path(), &output.path()).unwrap();

        // Ensure the blob manifest is correct.
        let manifest = std::fs::read_to_string(gendir.path().join("blob.manifest")).unwrap();
        let expected_manifest =
            format!("1739e556c9f2800c6263d8926ae00652d3c9a008b7a5ee501719854fe55b3787=file.txt\n",);
        assert_eq!(manifest, expected_manifest);

        // Ensure the blobs list and blobfs exists. We will not verify the
        // contents of these two files, because this code is not responsible
        // for creating those files. In order to make it easier to modify the
        // blobfs format in the future, that functionality should be tested
        // closer to the blobfs logic itself.
        assert!(Path::exists(&gendir.path().join("blobs.json")));
        assert!(Path::exists(&output.path()));
    }
}
