// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base_package::BasePackage;
use crate::config::{BlobFSConfig, ProductConfig};

use anyhow::{Context, Result};
use assembly_blobfs::BlobFSBuilder;
use assembly_images_config::BlobFS;
use assembly_tool::Tool;
use std::convert::TryInto;
use std::path::{Path, PathBuf};

pub fn convert_to_new_config(config: &BlobFSConfig) -> Result<BlobFS> {
    Ok(BlobFS {
        name: "blob".into(),
        layout: config.layout.as_str().try_into()?,
        compress: config.compress,
        maximum_bytes: None,
        minimum_data_bytes: None,
        minimum_inodes: None,
    })
}

pub fn construct_blobfs(
    blobfs_tool: Box<dyn Tool>,
    outdir: impl AsRef<Path>,
    gendir: impl AsRef<Path>,
    product: &ProductConfig,
    blobfs_config: &BlobFS,
    base_package: &BasePackage,
) -> Result<PathBuf> {
    let mut blobfs_builder = BlobFSBuilder::new(blobfs_tool, blobfs_config.layout.to_string());
    blobfs_builder.set_compressed(blobfs_config.compress);

    // Add the base and cache packages.
    for package_manifest_path in &product.base {
        blobfs_builder.add_package(&package_manifest_path)?;
    }
    for package_manifest_path in &product.cache {
        blobfs_builder.add_package(&package_manifest_path)?;
    }

    // Add the base package and its contents.
    blobfs_builder.add_file(&base_package.path)?;
    for (_, source) in &base_package.contents {
        blobfs_builder.add_file(source)?;
    }

    // Build the blobfs and return its path.
    let blobfs_path = outdir.as_ref().join("blob.blk");
    blobfs_builder.build(gendir, &blobfs_path).context("Failed to build the blobfs")?;
    Ok(blobfs_path)
}

#[cfg(test)]
mod tests {
    use super::{construct_blobfs, convert_to_new_config};
    use crate::base_package::BasePackage;
    use crate::config::{BlobFSConfig, ProductConfig};
    use assembly_images_config::{BlobFS, BlobFSLayout};
    use assembly_tool::testing::FakeToolProvider;
    use assembly_tool::ToolProvider;
    use fuchsia_hash::Hash;
    use std::collections::BTreeMap;
    use std::str::FromStr;
    use tempfile::tempdir;

    #[test]
    fn old_config() {
        let old_config = BlobFSConfig { layout: "compact".into(), compress: true };
        let new_config = convert_to_new_config(&old_config).unwrap();
        assert_eq!(new_config.layout, BlobFSLayout::Compact);
        assert_eq!(new_config.compress, true);

        let old_config = BlobFSConfig { layout: "deprecated_padded".into(), compress: false };
        let new_config = convert_to_new_config(&old_config).unwrap();
        assert_eq!(new_config.layout, BlobFSLayout::DeprecatedPadded);
        assert_eq!(new_config.compress, false);

        let old_config = BlobFSConfig { layout: "invalid".into(), compress: false };
        let result = convert_to_new_config(&old_config);
        assert!(result.is_err());
    }

    #[test]
    fn construct() {
        let dir = tempdir().unwrap();
        let product_config = ProductConfig::new("kernel", 0);
        let blobfs_config = BlobFS {
            name: "blob".into(),
            layout: BlobFSLayout::Compact,
            compress: true,
            maximum_bytes: None,
            minimum_data_bytes: None,
            minimum_inodes: None,
        };

        // Create a fake base package.
        let base_path = dir.path().join("base.far");
        std::fs::write(&base_path, "fake base").unwrap();
        let base = BasePackage {
            merkle: Hash::from_str(
                "0000000000000000000000000000000000000000000000000000000000000000",
            )
            .unwrap(),
            contents: BTreeMap::default(),
            path: base_path,
        };

        // Create a fake blobfs tool.
        let tools = FakeToolProvider::default();
        let blobfs_tool = tools.get_tool("blobfs").unwrap();

        // Construct blobfs, and ensure no error is returned.
        construct_blobfs(
            blobfs_tool,
            dir.path(),
            dir.path(),
            &product_config,
            &blobfs_config,
            &base,
        )
        .unwrap();
    }
}
