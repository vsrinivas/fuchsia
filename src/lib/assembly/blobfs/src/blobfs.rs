// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::manifest::BlobManifest;
use anyhow::{Context, Result};
use assembly_tool::Tool;
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
/// let builder = BlobFSBuilder::new(blobfs_tool, "compact");
/// builder.set_compressed(false);
/// builder.add_package("path/to/package_manifest.json")?;
/// builder.add_file("path/to/file.txt")?;
/// builder.build(gendir, "blob.blk")?;
/// ```
///
pub struct BlobFSBuilder {
    tool: Box<dyn Tool>,
    layout: String,
    compress: bool,
    manifest: BlobManifest,
}

impl BlobFSBuilder {
    /// Construct a new BlobFSBuilder.
    pub fn new(tool: Box<dyn Tool>, layout: impl AsRef<str>) -> Self {
        BlobFSBuilder {
            tool,
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
        self.tool.run(&blobfs_args)
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

    match blob_layout.as_str() {
        "deprecated_padded" => {
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
    use assembly_tool::testing::FakeToolProvider;
    use assembly_tool::{ToolCommandLog, ToolProvider};
    use serde_json::json;
    use std::io::Write;
    use tempfile::TempDir;

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
        let dir = TempDir::new().unwrap();

        // Create a test file.
        let filepath = dir.path().join("file.txt");
        let mut file = File::create(&filepath).unwrap();
        write!(file, "Boaty McBoatface").unwrap();

        // Get the path of the output.
        let output_path = dir.path().join("blob.blk");
        let output_path_str = output_path.path_to_string().unwrap();

        // Build blobfs.
        let tools = FakeToolProvider::default();
        let blobfs_tool = tools.get_tool("blobfs").unwrap();
        let mut builder = BlobFSBuilder::new(blobfs_tool, "compact");
        builder.set_compressed(true);
        builder.add_file(&filepath).unwrap();
        builder.build(&dir.path(), output_path).unwrap();
        drop(builder);

        // Ensure the command was run correctly.
        let blobs_json_path = dir.path().join("blobs.json").path_to_string().unwrap();
        let blob_manifest_path = dir.path().join("blob.manifest").path_to_string().unwrap();
        let expected_commands: ToolCommandLog = serde_json::from_value(json!({
            "commands": [
                {
                    "tool": "./host_x64/blobfs",
                    "args": [
                        "--json-output",
                        blobs_json_path,
                        "--compress",
                        output_path_str,
                        "create",
                        "--manifest",
                        blob_manifest_path,
                    ]
                }
            ]
        }))
        .unwrap();
        assert_eq!(&expected_commands, tools.log());
    }
}
