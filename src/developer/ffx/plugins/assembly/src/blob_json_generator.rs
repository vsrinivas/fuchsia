// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::operations::size_check_package::BlobJsonEntry;
use crate::util::read_config;
use anyhow::{Context, Result};
use assembly_images_config::BlobFSLayout;
use assembly_tool::ToolProvider;
use camino::Utf8Path;
use tempfile::NamedTempFile;
use tempfile::TempDir;

/// Collect all the blob size entries for a given set of packages.
pub struct BlobJsonGenerator {
    /// The layout format of the blobs.
    layout: BlobFSLayout,
    /// The tools provider that contains the blobfs tool.
    tools: Box<dyn ToolProvider>,
}

impl BlobJsonGenerator {
    /// Reads the specified configuration and return an object capable to build blobfs.
    pub fn new(tools: Box<dyn ToolProvider>, layout: BlobFSLayout) -> Result<BlobJsonGenerator> {
        Ok(BlobJsonGenerator { layout, tools })
    }

    /// Returns information blobs used by the specified packages.
    pub fn build(&self, package_manifests: &Vec<&Utf8Path>) -> Result<Vec<BlobJsonEntry>> {
        let mut builder = assembly_blobfs::BlobFSBuilder::new(
            self.tools.get_tool("blobfs")?,
            self.layout.to_string(),
        );
        // Currently, we only care about doing size checks on products with compressed blobfs. We
        // can make this dynamic if the need arises.
        builder.set_compressed(true);
        package_manifests
            .iter()
            .map(|manifest| builder.add_package(&manifest))
            .collect::<Result<Vec<()>>>()?;

        let tmp = TempDir::new()?;
        let tmp_working_dir = Utf8Path::from_path(tmp.path()).context("creating temp directory")?;

        let blobfs_named_file_tmp = NamedTempFile::new()?;
        let blobfs_named_file_path =
            Utf8Path::from_path(blobfs_named_file_tmp.path()).context("creating temp file")?;

        builder.build(tmp_working_dir, blobfs_named_file_path)?;
        read_config(tmp_working_dir.join("blobs.json"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::write_json_file;
    use assembly_images_config::BlobFSLayout;
    use assembly_tool::testing::FakeToolProvider;
    use camino::Utf8Path;
    use fuchsia_hash::Hash;
    use serde_json::json;
    use std::path::Path;
    use std::str::FromStr;

    #[test]
    fn generate_blobfs() {
        let temp_dir = TempDir::new().unwrap();
        let root = Utf8Path::from_path(temp_dir.path()).unwrap();

        let my_content_path = root.join("my_content.txt");
        write_json_file(&my_content_path, &json!("some file content")).unwrap();
        let manifest_path = root.join("my_package.json");
        write_json_file(
            &manifest_path,
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
        let blobs_path = root.join("blobs.json");
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
        let gen = BlobJsonGenerator::new(tool_provider, BlobFSLayout::DeprecatedPadded).unwrap();
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
