// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use std::io::Read;
use std::path::PathBuf;

#[derive(Deserialize, Serialize)]
pub struct ProductConfig {
    pub version: String,
    pub extra_packages_for_base_package: Vec<PathBuf>,
    pub base_packages: Vec<PathBuf>,
    pub cache_packages: Vec<PathBuf>,
    pub meta_packages: Vec<PathBuf>,
    pub kernel_image: PathBuf,
    pub kernel_cmdline: Vec<String>,
    pub bootfs_files: Vec<BootFsEntry>,
}

#[derive(Deserialize, Serialize)]
pub struct BoardConfig {
    pub name: String,
    pub vbmeta_key: PathBuf,
    pub vbmeta_key_metadata: PathBuf,
    pub bootloader: PathBuf,
    pub fvm: FvmConfig,
}

#[derive(Deserialize, Serialize)]
pub struct BootFsEntry {
    pub source: PathBuf,
    pub destination: String,
}

#[derive(Deserialize, Serialize)]
pub struct FvmConfig {
    pub slice_size: u64,
    pub reserved_slices: u64,
    pub blob: FvmPartitionConfig,
    pub data: FvmPartitionConfig,
}

#[derive(Deserialize, Serialize)]
pub struct FvmPartitionConfig {
    pub layout_format: Option<String>,
    pub minimum_inodes: u64,
    pub minimum_data_size: u64,
    pub maximum_bytes: u64,
}

pub fn from_reader<R, T>(reader: &mut R) -> Result<T>
where
    R: Read,
    T: serde::de::DeserializeOwned,
{
    let mut data = String::default();
    reader.read_to_string(&mut data).context("Cannot read the config")?;
    serde_json5::from_str(&data).context("Cannot parse the config")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn product_from_json_file() {
        let json = r#"
            {
              version: "0.1.2",
              extra_packages_for_base_package: ["package0"],
              base_packages: ["package1", "package2"],
              cache_packages: ["package3", "package4"],
              meta_packages: ["package5", "package6"],
              kernel_image: "path/to/kernel",
              kernel_cmdline: ["arg1", "arg2"],
              bootfs_files: [
                {
                    source: "path/to/source",
                    destination: "path/to/destination",
                },
              ],
            }
        "#;

        let mut cursor = std::io::Cursor::new(json);
        let config: ProductConfig = from_reader(&mut cursor).expect("parse config");
        assert_eq!(config.version, "0.1.2");
    }

    #[test]
    fn board_from_json_file() {
        let json = r#"
            {
              name: "my-board",
              vbmeta_key: "key",
              vbmeta_key_metadata: "metadata",
              bootloader: "path/to/bootloader",
              fvm: {
                slice_size: 1,
                reserved_slices: 1,
                blob: {
                  layout_format: "compact",
                  minimum_inodes: 1,
                  minimum_data_size: 1,
                  maximum_bytes: 1,
                },
                data: {
                  minimum_inodes: 1,
                  minimum_data_size: 1,
                  maximum_bytes: 1,
                },
              },
            }
         "#;

        let mut cursor = std::io::Cursor::new(json);
        let config: BoardConfig = from_reader(&mut cursor).expect("parse config");
        assert_eq!(config.name, "my-board");
    }

    #[test]
    fn product_from_invalid_json_file() {
        let json = r#"
            {
            }
        "#;

        let mut cursor = std::io::Cursor::new(json);
        let config: Result<ProductConfig> = from_reader(&mut cursor);
        assert!(config.is_err());
    }

    #[test]
    fn board_from_invalid_json_file() {
        let json = r#"
            {
            }
        "#;

        let mut cursor = std::io::Cursor::new(json);
        let config: Result<BoardConfig> = from_reader(&mut cursor);
        assert!(config.is_err());
    }
}
