// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::manifest::BlobManifest;
use anyhow::{Context, Result};
use assembly_tool::Tool;
use assembly_util::PathToStringExt;
use camino::{Utf8Path, Utf8PathBuf};
use fuchsia_pkg::PackageManifest;
use serde::Deserialize;
use std::fs::File;
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
    pub fn add_package(&mut self, package_manifest_path: impl AsRef<Utf8Path>) -> Result<()> {
        let package_manifest_path = package_manifest_path.as_ref();
        let manifest = PackageManifest::try_load_from(package_manifest_path)?;
        self.manifest.add_package(manifest)
    }

    /// Add a file to blobfs from the `path` on the host.
    pub fn add_file(&mut self, path: impl AsRef<Utf8Path>) -> Result<()> {
        self.manifest.add_file(path.as_ref())
    }

    /// Build blobfs, and write it to `output`, while placing intermediate files in `gendir`.
    pub fn build(
        &self,
        gendir: impl AsRef<Utf8Path>,
        output: impl AsRef<Utf8Path>,
    ) -> Result<Utf8PathBuf> {
        // Delete the output file if it exists.
        let output = output.as_ref();
        if output.exists() {
            std::fs::remove_file(&output)
                .with_context(|| format!("Failed to delete previous blobfs file: {}", output))?;
        }

        // Write the blob manifest.
        let blob_manifest_path = gendir.as_ref().join("blob.manifest");
        self.manifest.write(&blob_manifest_path).context("Failed to write to blob.manifest")?;

        let blobs_json_path = gendir.as_ref().join("blobs.json");
        // Build the arguments vector.
        let blobfs_args = build_blobfs_args(
            self.layout.clone(),
            self.compress,
            &blob_manifest_path,
            blobs_json_path.clone(),
            output,
        )?;

        // Run the blobfs tool.
        let result = self.tool.run(&blobfs_args);
        match result {
            Ok(_) => Ok(blobs_json_path.clone()),
            Err(e) => Err(e),
        }
    }

    /// Read blobs.json file into BlobsJson struct
    pub fn read_blobs_json(&self, path_buf: impl AsRef<Utf8Path>) -> anyhow::Result<BlobsJson> {
        let mut file = File::open(path_buf.as_ref())
            .context(format!("Unable to open file blobs json file"))?;
        let blobs_json: BlobsJson =
            assembly_util::from_reader(&mut file).context("Failed to read blobs json file")?;
        Ok(blobs_json)
    }
}

// #[derive(Debug, Deserialize, PartialEq, Eq)]
// pub struct BlobsJson(pub Vec<BlobJsonEntry>);
type BlobsJson = Vec<BlobJsonEntry>;

#[derive(Debug, Deserialize, PartialEq, Eq)]
pub struct BlobJsonEntry {
    pub merkle: String,
    pub used_space_in_blobfs: u64,
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
    use std::fs::File;
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
        let tmp = TempDir::new().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        // Create a test file.
        let filepath = dir.join("file.txt");
        let mut file = File::create(&filepath).unwrap();
        write!(file, "Boaty McBoatface").unwrap();

        // Get the path of the output.
        let output_path = dir.join("blob.blk");
        let output_path_str = output_path.to_string();

        // Build blobfs.
        let tools = FakeToolProvider::new_with_side_effect(|_name, args| {
            let blobs_file_path = &args[1];
            let mut blobs_file = File::create(&blobs_file_path).unwrap();
            let file_contents = r#"[  
                {
                  "merkle": "000000000000000000000000000000000000000000000000000000000003212e",
                  "used_space_in_blobfs": 4096
                },
                {
                  "merkle": "00000000000000000000000000000000000000000000000000000000000ddf28",
                  "used_space_in_blobfs": 2048
                },
                {
                  "merkle": "00000000000000000000000000000000000000000000000000000000000e593d",
                  "used_space_in_blobfs": 1024
                },
              ]
              "#;
            write!(blobs_file, "{}", file_contents).unwrap()
        });
        let blobfs_tool = tools.get_tool("blobfs").unwrap();
        let mut builder = BlobFSBuilder::new(blobfs_tool, "compact");
        builder.set_compressed(true);
        builder.add_file(&filepath).unwrap();

        let blobs_json_path = builder.build(&dir, output_path).unwrap();
        let actual_blobs_json = builder.read_blobs_json(blobs_json_path).unwrap();
        let expected_blobs_json = vec![
            BlobJsonEntry {
                merkle: "000000000000000000000000000000000000000000000000000000000003212e"
                    .to_string(),
                used_space_in_blobfs: 4096,
            },
            BlobJsonEntry {
                merkle: "00000000000000000000000000000000000000000000000000000000000ddf28"
                    .to_string(),
                used_space_in_blobfs: 2048,
            },
            BlobJsonEntry {
                merkle: "00000000000000000000000000000000000000000000000000000000000e593d"
                    .to_string(),
                used_space_in_blobfs: 1024,
            },
        ];

        assert_eq!(expected_blobs_json, actual_blobs_json);

        drop(builder);

        // Ensure the command was run correctly.
        let blobs_json_path = dir.join("blobs.json");
        let blob_manifest_path = dir.join("blob.manifest");
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
