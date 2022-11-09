// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base_package::BasePackage;

use anyhow::{Context, Result};
use assembly_blobfs::BlobFSBuilder;
use assembly_config_schema::ImageAssemblyConfig;
use assembly_images_config::BlobFS;
use assembly_manifest::BlobfsContents;
use assembly_tool::Tool;
use camino::{Utf8Path, Utf8PathBuf};
use std::collections::HashMap;

pub fn construct_blobfs(
    blobfs_tool: Box<dyn Tool>,
    outdir: impl AsRef<Utf8Path>,
    gendir: impl AsRef<Utf8Path>,
    image_config: &ImageAssemblyConfig,
    blobfs_config: &BlobFS,
    base_package: &BasePackage,
) -> Result<(Utf8PathBuf, BlobfsContents)> {
    let mut contents = BlobfsContents::default();
    let mut blobfs_builder = BlobFSBuilder::new(blobfs_tool, blobfs_config.layout.to_string());
    blobfs_builder.set_compressed(blobfs_config.compress);
    contents.maximum_contents_size = blobfs_config.maximum_contents_size.clone();

    // Add the base and cache packages.
    for package_manifest_path in &image_config.base {
        blobfs_builder.add_package(&package_manifest_path)?;
    }
    for package_manifest_path in &image_config.cache {
        blobfs_builder.add_package(&package_manifest_path)?;
    }

    // Add the base package and its contents.
    blobfs_builder.add_package(&base_package.manifest_path)?;

    // Build the blobfs and store the merkle to size map.
    let blobfs_path = outdir.as_ref().join("blob.blk");
    let blobs_json_path =
        blobfs_builder.build(gendir, &blobfs_path).context("Failed to build the blobfs")?;
    let merkle_size_map = match blobfs_builder.read_blobs_json(blobs_json_path) {
        Ok(blobs_json) => {
            blobs_json.iter().map(|e| (e.merkle.to_string(), e.used_space_in_blobfs)).collect()
        }
        Err(_) => HashMap::default(),
    };
    for package_manifest_path in &image_config.base {
        contents.add_base_package(package_manifest_path, &merkle_size_map)?;
    }
    for package_manifest_path in &image_config.cache {
        contents.add_cache_package(package_manifest_path, &merkle_size_map)?;
    }
    contents.add_base_package(&base_package.manifest_path, &merkle_size_map)?;
    Ok((blobfs_path, contents))
}

#[cfg(test)]
mod tests {
    use super::construct_blobfs;
    use crate::base_package::BasePackage;
    use assembly_config_schema::ImageAssemblyConfig;
    use assembly_images_config::{BlobFS, BlobFSLayout};
    use assembly_tool::testing::FakeToolProvider;
    use assembly_tool::ToolProvider;
    use camino::Utf8Path;
    use fuchsia_hash::Hash;
    use std::collections::BTreeMap;
    use std::fs::File;
    use std::io::Write;
    use std::str::FromStr;
    use tempfile::tempdir;

    #[test]
    fn construct() {
        let tmp = tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let image_config = ImageAssemblyConfig::new_for_testing("kernel", 0);
        let blobfs_config = BlobFS {
            name: "blob".into(),
            layout: BlobFSLayout::Compact,
            compress: true,
            maximum_bytes: None,
            minimum_data_bytes: None,
            minimum_inodes: None,
            maximum_contents_size: None,
        };

        // Create a fake base package.
        let base_path = dir.join("base.far");
        std::fs::write(&base_path, "fake base").unwrap();
        let base_package_manifest_path = dir.join("package_manifest.json");
        let mut base_package_manifest_file = File::create(&base_package_manifest_path).unwrap();
        let contents = r#"{
            "version": "1",
            "package": {
                "name": "system_image",
                "version": "0"
            },
            "blobs": []
        }
        "#;
        write!(base_package_manifest_file, "{}", contents).unwrap();
        let base = BasePackage {
            merkle: Hash::from_str(
                "0000000000000000000000000000000000000000000000000000000000000000",
            )
            .unwrap(),
            contents: BTreeMap::default(),
            path: base_path,
            manifest_path: base_package_manifest_path,
        };

        // Create a fake blobfs tool.
        let tools = FakeToolProvider::default();
        let blobfs_tool = tools.get_tool("blobfs").unwrap();

        // Construct blobfs, and ensure no error is returned.
        construct_blobfs(blobfs_tool, dir, dir, &image_config, &blobfs_config, &base).unwrap();
    }
}
