// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config::BoardConfig;
use crate::operations::size_check::BlobJsonEntry;
use crate::util::read_config;
use anyhow::Context;
use anyhow::Result;
use assembly_tool::ToolProvider;
use std::path::Path;
use tempfile::NamedTempFile;
use tempfile::TempDir;

/// Collect all the blob size entries for a given set of packages.
pub struct BlobJsonGenerator {
    /// The layout format of the blobs.
    /// Typically "deprecated_padded" or "compact".
    layout: String,
    /// Whenever the blobs are compressed, or not.
    compress: bool,

    tools: Box<dyn ToolProvider>,
}

impl BlobJsonGenerator {
    /// Reads the specified configuration and return an object capable to build blobfs.
    pub fn new(tools: Box<dyn ToolProvider>, board_config: &Path) -> Result<BlobJsonGenerator> {
        let board_config: BoardConfig =
            read_config(board_config).context("Failed to read the board config")?;
        Ok(BlobJsonGenerator {
            layout: board_config.blobfs.layout,
            compress: board_config.blobfs.compress,
            tools,
        })
    }

    /// Returns information blobs used by the specified packages.
    pub fn build(&self, package_manifests: &Vec<&Path>) -> Result<Vec<BlobJsonEntry>> {
        let mut builder =
            assembly_blobfs::BlobFSBuilder::new(self.tools.get_tool("blobfs")?, &self.layout);
        builder.set_compressed(self.compress);
        package_manifests
            .iter()
            .map(|manifest| builder.add_package(&manifest))
            .collect::<Result<Vec<()>>>()?;

        let tmp_working_dir = TempDir::new()?;
        let blobfs_named_file = NamedTempFile::new()?;
        builder.build(tmp_working_dir.path(), blobfs_named_file.path())?;
        read_config(tmp_working_dir.path().join("blobs.json"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::write_json_file;
    use assembly_tool::testing::FakeToolProvider;
    use fuchsia_hash::Hash;
    use serde_json::json;
    use std::path::Path;
    use std::str::FromStr;

    #[test]
    fn generate_blobfs() {
        let temp_dir = TempDir::new().unwrap();
        let board_config_path = temp_dir.path().join("board_config.json");
        write_json_file(
            &board_config_path,
            &json!({
              "blobfs": {
                  "layout": "deprecated_padded",
                  "compress": true
              }
            }),
        )
        .unwrap();
        let my_content_path = temp_dir.path().join("my_content.txt");
        write_json_file(&my_content_path, &json!("some file content")).unwrap();
        let manifest_path = temp_dir.path().join("my_package.json");
        write_json_file(
            manifest_path.as_path(),
            &json!({
              "version": "1",
              "package": {
                  "name": "pkg-cache",
                  "version": "0"
              },
              "blobs" : [{
                  "source_path": my_content_path,
                  "path": "my_content",
                  "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                  "size": 8i32
              }]
            }),
        )
        .unwrap();
        let blobs_path = temp_dir.path().join("blobs.json");
        write_json_file(
            blobs_path.as_path(),
            &json!([{
                "merkle": "0e56473237b6b2ce39358c11a0fbd2f89902f246d966898d7d787c9025124d51",
                "size": 8i32
            },{
                "merkle": "b62ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2f",
                "size": 32i32
            },{
                "merkle": "01ecd6256f89243e1f0f7d7022cc2e8eb059b06c941d334d9ffb108478749646",
                "size": 128i32
            }]),
        )
        .unwrap();
        let tool_provider =
            Box::new(FakeToolProvider::new_with_side_effect(|_name: &str, args: &[String]| {
                assert_eq!(args[0], "--json-output");
                write_json_file(
                    Path::new(&args[1]),
                    &json!([{
                      "merkle": "b62ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2a",
                      "size": 73i32
                    }]),
                )
                .unwrap();
            }));
        let gen = BlobJsonGenerator::new(tool_provider, &board_config_path).unwrap();
        let blob_entries = gen.build(&vec![&manifest_path]).unwrap();
        assert_eq!(
            vec!(BlobJsonEntry {
                merkle: Hash::from_str(
                    "b62ee413090825c2ae70fe143b34cbd851f055932cfd5e7ca4ef0efbb802da2a"
                )
                .unwrap(),
                size: 73
            }),
            blob_entries
        );
    }
}
