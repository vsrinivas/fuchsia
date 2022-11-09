// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use camino::Utf8PathBuf;
use serde::{Deserialize, Serialize};
use std::io::Read;

/// The configuration file specifying where the generated images should be placed when flashing of
/// OTAing. This file lists the partitions used in three different flashing configurations:
///   fuchsia      - primary images in A/B, recovery in R, bootloaders, bootstrap
///   fuchsia_only - primary images in A/B, recovery in R, bootloaders
///   recovery     - recovery in A/B/R, bootloaders
#[derive(Clone, Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct PartitionsConfig {
    /// Partitions that are only flashed in "fuchsia" configurations.
    #[serde(default)]
    pub bootstrap_partitions: Vec<BootstrapPartition>,

    /// Partitions designated for bootloaders, which are not slot-specific.
    pub bootloader_partitions: Vec<BootloaderPartition>,

    /// Non-bootloader partitions, which are slot-specific.
    pub partitions: Vec<Partition>,

    /// The name of the hardware to assert before flashing images to partitions.
    pub hardware_revision: String,

    /// Zip files containing the fastboot unlock credentials.
    #[serde(default)]
    pub unlock_credentials: Vec<Utf8PathBuf>,
}

impl PartitionsConfig {
    /// Parse the config from a reader.
    pub fn from_reader<R>(reader: &mut R) -> Result<Self>
    where
        R: Read,
    {
        let mut data = String::default();
        reader.read_to_string(&mut data).context("Cannot read the config")?;
        serde_json5::from_str(&data).context("Cannot parse the config")
    }
}

/// A partition to flash in "fuchsia" configurations.
#[derive(Clone, Serialize, Deserialize, Debug, PartialEq)]
pub struct BootstrapPartition {
    /// The name of the partition known to fastboot.
    pub name: String,

    /// The path on host to the bootloader image.
    pub image: Utf8PathBuf,

    /// The condition that must be met before attempting to flash.
    pub condition: Option<BootstrapCondition>,
}

/// The fastboot variable condition that must equal the value before a bootstrap partition should
/// be flashed.
#[derive(Clone, Serialize, Deserialize, Debug, PartialEq)]
pub struct BootstrapCondition {
    /// The name of the fastboot variable.
    pub variable: String,

    /// The expected value.
    pub value: String,
}

/// A single bootloader partition, which is not slot-specific.
#[derive(Clone, Serialize, Deserialize, Debug, PartialEq)]
pub struct BootloaderPartition {
    /// The firmware type provided to the update system.
    /// See documentation here:
    ///     https://fuchsia.dev/fuchsia-src/concepts/packages/update_pkg
    #[serde(rename = "type")]
    pub partition_type: String,

    /// The name of the partition known to fastboot.
    /// If the name is not provided, then the partition should not be flashed.
    pub name: Option<String>,

    /// The path on host to the bootloader image.
    pub image: Utf8PathBuf,
}

/// A non-bootloader partition which
#[derive(Clone, Serialize, Deserialize, Debug, PartialEq)]
#[serde(tag = "type")]
pub enum Partition {
    /// A partition prepared for the Zircon Boot Image (ZBI).
    ZBI {
        /// The partition name.
        name: String,
        /// The slot of the partition.
        slot: Slot,
    },

    /// A partition prepared for the Verified Boot Metadata (VBMeta).
    VBMeta {
        /// The partition name.
        name: String,
        /// The slot of the partition.
        slot: Slot,
    },

    /// A partition prepared for the Fuchsia Volume Manager (FVM).
    FVM {
        /// The partition name.
        name: String,
    },
}

/// The slots available for flashing or OTAing.
#[derive(Serialize, Deserialize, Debug, PartialOrd, Ord, PartialEq, Eq, Clone, Copy)]
pub enum Slot {
    /// Primary slot.
    A,

    /// Alternate slot.
    B,

    /// Recovery slot.
    R,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn from_json() {
        let json = r#"
            {
                bootloader_partitions: [
                    {
                        type: "tpl",
                        name: "firmware_tpl",
                        image: "path/to/image",
                    }
                ],
                partitions: [
                    {
                        type: "ZBI",
                        name: "zircon_a",
                        slot: "A",
                    },
                    {
                        type: "VBMeta",
                        name: "vbmeta_b",
                        slot: "B",
                    },
                    {
                        type: "FVM",
                        name: "fvm",
                    },
                ],
                hardware_revision: "hw",
                unlock_credentials: [
                    "path/to/zip",
                ],
            }
        "#;
        let mut cursor = std::io::Cursor::new(json);
        let config: PartitionsConfig = PartitionsConfig::from_reader(&mut cursor).unwrap();
        assert_eq!(config.partitions.len(), 3);
        assert_eq!(config.hardware_revision, "hw");
    }

    #[test]
    fn invalid_partition_type() {
        let json = r#"
            {
                bootloader_partitions: [],
                partitions: [
                    {
                        type: "Invalid",
                        name: "zircon",
                        slot: "SlotA",
                    }
                ],
                "hardware_revision": "hw",
            }
        "#;
        let mut cursor = std::io::Cursor::new(json);
        let config: Result<PartitionsConfig> = PartitionsConfig::from_reader(&mut cursor);
        assert!(config.is_err());
    }
}
