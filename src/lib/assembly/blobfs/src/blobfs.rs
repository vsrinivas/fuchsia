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
/// let builder = BlobFSBuilder::new("compact");
/// builder.set_compressed(false);
/// builder.add_package("path/to/package_manifest.json")?;
/// builder.add_file("path/to/file.txt")?;
/// builder.build(gendir, "blob.blk")?;
/// ```
///
pub struct BlobFSBuilder {
    layout: String,
    compress: bool,
    manifest: BlobManifest,
}

impl BlobFSBuilder {
    /// Construct a new BlobFSBuilder.
    pub fn new(layout: impl AsRef<str>) -> Self {
        BlobFSBuilder {
            layout: layout.as_ref().to_string(),
            compress: false,
            manifest: BlobManifest::default(),
        }
    }

    /// Set whether the blobs should be compressed inside BlobFS.
    pub fn set_compressed(&mut self, compress: bool) {
        self.compress = compress;
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
        // Delete the output file if it exists.
        if output.as_ref().exists() {
            std::fs::remove_file(&output).context(format!(
                "Failed to delete previous blobfs file: {}",
                output.as_ref().display()
            ))?;
        }

        // Write the blob manifest.
        let blob_manifest_path = gendir.as_ref().join("blob.manifest");
        self.manifest.write(&blob_manifest_path).context("Failed to write to blob.manifest")?;

        // Build the arguments vector.
        let blobs_json_path = gendir.as_ref().join("blobs.json");
        let blobfs_args = build_blobfs_args(
            self.layout.clone(),
            self.compress,
            &blob_manifest_path,
            blobs_json_path,
            output,
        )?;

        // Run the blobfs tool.
        // TODO(fxbug.dev/76378): Take the tool location from a config.
        let output = std::process::Command::new("host_x64/blobfs").args(&blobfs_args).output();
        let output = output.context("Failed to run the blobfs tool")?;
        if !output.status.success() {
            anyhow::bail!(format!(
                "Failed to generate blobfs with status: {}\n{}",
                output.status,
                String::from_utf8_lossy(output.stderr.as_slice())
            ));
        }
        Ok(())
    }
}

/// Build the list of arguments to pass to the blobfs tool.
fn build_blobfs_args(
    blob_layout: String,
    compress: bool,
    blob_manifest_path: impl AsRef<Path>,
    blobs_json_path: impl AsRef<Path>,
    output_path: impl AsRef<Path>,
) -> Result<Vec<String>> {
    let mut args = vec!["--json-output".to_string(), blobs_json_path.as_ref().path_to_string()?];
    if compress {
        args.push("--compress".to_string());
    }
    args.extend([
        output_path.as_ref().path_to_string()?,
        "create".to_string(),
        "--manifest".to_string(),
        blob_manifest_path.as_ref().path_to_string()?,
    ]);

    // TODO(fxbug.dev/81353): Remove "padded" and use only "deprecated_padded" for the value once
    // all uses have been updated.
    match blob_layout.as_str() {
        "deprecated_padded" | "padded" => {
            args.push("--deprecated_padded_format".to_string());
        }
        "compact" => {}
        _ => {
            anyhow::bail!("blob_layout is invalid");
        }
    }
    Ok(args)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::{NamedTempFile, TempDir};

    #[test]
    fn blobfs_args() {
        let args = build_blobfs_args(
            "compact".to_string(),
            true,
            "blob.manifest",
            "blobs.json",
            "blob.blk",
        )
        .unwrap();
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
            ]
        );
    }

    #[test]
    fn blobfs_args_no_compress() {
        let args = build_blobfs_args(
            "deprecated_padded".to_string(),
            false,
            "blob.manifest",
            "blobs.json",
            "blob.blk",
        )
        .unwrap();
        assert_eq!(
            args,
            vec![
                "--json-output",
                "blobs.json",
                "blob.blk",
                "create",
                "--manifest",
                "blob.manifest",
                "--deprecated_padded_format",
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
        let mut builder = BlobFSBuilder::new("compact");
        builder.set_compressed(true);
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
