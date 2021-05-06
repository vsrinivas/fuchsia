// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ffx_core::{ffx_error, FfxError},
    serde::{Deserialize, Serialize},
    std::io::Read,
    std::path::PathBuf,
};

#[derive(Deserialize, Serialize)]
pub(crate) struct Config {
    pub extra_packages_for_base_package: Vec<PathBuf>,
    pub base_packages: Vec<PathBuf>,
    pub cache_packages: Vec<PathBuf>,
    pub meta_packages: Vec<PathBuf>,
    pub kernel_image: PathBuf,
    pub kernel_cmdline: Vec<String>,
    pub bootfs_files: Vec<BootFsEntry>,
    pub vbmeta_key: PathBuf,
    pub vbmeta_key_metadata: PathBuf,
    pub version: String,
    pub board: BoardConfig,
}

#[derive(Deserialize, Serialize)]
pub(crate) struct BootFsEntry {
    pub source: PathBuf,
    pub destination: String,
}

#[derive(Deserialize, Serialize)]
pub(crate) struct BoardConfig {
    pub name: String,
    pub bootloader: PathBuf,
    pub fvm: FvmConfig,
}

#[derive(Deserialize, Serialize)]
pub(crate) struct FvmConfig {
    pub slice_size: u64,
    pub reserved_slices: u64,
    pub blob: FvmPartitionConfig,
    pub data: FvmPartitionConfig,
}

#[derive(Deserialize, Serialize)]
pub(crate) struct FvmPartitionConfig {
    pub layout_format: Option<String>,
    pub minimum_inodes: u64,
    pub minimum_data_size: u64,
    pub maximum_bytes: u64,
}

impl Config {
    pub(crate) fn from_reader(reader: &mut impl Read) -> Result<Self, FfxError> {
        let mut data = String::default();
        reader
            .read_to_string(&mut data)
            .map_err(|e| ffx_error!("Cannot read the config: {}", e))?;
        serde_json5::from_str(&data).map_err(|e| ffx_error!("Cannot parse the config: {}", e))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn from_json_file() {
        let json = r#"
            {
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
              vbmeta_key: "key",
              vbmeta_key_metadata: "metadata",
              version: "0.1.2",
              board: {
                name: "my-board",
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
              },
            }
        "#;

        let mut cursor = std::io::Cursor::new(json);
        let config = Config::from_reader(&mut cursor).expect("parse config");
        assert_eq!(config.version, "0.1.2");
    }

    #[test]
    fn from_invalid_json_file() {
        let json = r#"
            {
            }
        "#;

        let mut cursor = std::io::Cursor::new(json);
        let config = Config::from_reader(&mut cursor);
        assert!(config.is_err());
    }
}
