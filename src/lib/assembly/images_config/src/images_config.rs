// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Result};
use camino::Utf8PathBuf;
use serde::{Deserialize, Serialize};
use std::convert::TryFrom;
use std::fmt;
use std::io::Read;
use std::path::PathBuf;
use std::str::FromStr;

/// The configuration file specifying which images to generate and how.
#[derive(Serialize, Deserialize, Debug, Default)]
pub struct ImagesConfig {
    /// A list of images to generate.
    #[serde(default)]
    pub images: Vec<Image>,
}

/// An image to generate.
#[derive(Serialize, Deserialize, Debug)]
#[serde(tag = "type")]
pub enum Image {
    /// A FVM image.
    #[serde(rename = "fvm")]
    Fvm(Fvm),

    /// A ZBI image.
    #[serde(rename = "zbi")]
    Zbi(Zbi),

    /// A VBMeta image.
    #[serde(rename = "vbmeta")]
    VBMeta(VBMeta),
}

/// Parameters describing how to generate the ZBI.
#[derive(Serialize, Deserialize, Debug)]
pub struct Zbi {
    /// The name to give the image file.
    #[serde(default = "default_zbi_name")]
    pub name: String,

    /// The compression format for the ZBI.
    #[serde(default = "default_zbi_compression")]
    pub compression: ZbiCompression,

    /// An optional script to post-process the ZBI.
    /// This is often used to prepare the ZBI for flashing/updating.
    #[serde(default)]
    pub postprocessing_script: Option<PostProcessingScript>,
}

fn default_zbi_name() -> String {
    "fuchsia".into()
}

fn default_zbi_compression() -> ZbiCompression {
    ZbiCompression::ZStd
}

/// The compression format for the ZBI.
#[derive(Serialize, Deserialize, Debug, PartialEq)]
pub enum ZbiCompression {
    /// zstd compression.
    #[serde(rename = "zstd")]
    ZStd,

    /// zstd.max compression.
    #[serde(rename = "zstd.max")]
    ZStdMax,
}

impl FromStr for ZbiCompression {
    type Err = anyhow::Error;
    fn from_str(s: &str) -> Result<Self> {
        zbi_compression_from_str(s)
    }
}

impl TryFrom<&str> for ZbiCompression {
    type Error = anyhow::Error;
    fn try_from(s: &str) -> Result<Self> {
        zbi_compression_from_str(s)
    }
}

impl fmt::Display for ZbiCompression {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}",
            match self {
                ZbiCompression::ZStd => "zstd",
                ZbiCompression::ZStdMax => "zstd.max",
            }
        )
    }
}

fn zbi_compression_from_str(s: &str) -> Result<ZbiCompression> {
    match s {
        "zstd" => Ok(ZbiCompression::ZStd),
        "zstd.max" => Ok(ZbiCompression::ZStdMax),
        invalid => Err(anyhow!("invalid zbi compression: {}", invalid)),
    }
}

/// A script to process the ZBI after it is constructed.
#[derive(Serialize, Deserialize, Debug)]
pub struct PostProcessingScript {
    /// The path to the script on host.
    /// This script _musts_ take the following arguments:
    ///   -z <path to ZBI>
    ///   -o <output path>
    ///   -B <build directory, relative to script's source directory>
    pub path: PathBuf,

    /// Additional arguments to pass to the script after the above arguments.
    #[serde(default)]
    pub args: Vec<String>,
}

/// The parameters describing how to create a VBMeta image.
#[derive(Serialize, Deserialize, Debug)]
pub struct VBMeta {
    /// The name to give the image file.
    #[serde(default = "default_vbmeta_name")]
    pub name: String,

    /// Path on host to the key for signing VBMeta.
    pub key: Utf8PathBuf,

    /// Path on host to the key metadata to add to the VBMeta.
    pub key_metadata: Utf8PathBuf,

    /// Optional descriptors to add to the VBMeta image.
    #[serde(default)]
    pub additional_descriptors: Vec<VBMetaDescriptor>,
}

fn default_vbmeta_name() -> String {
    "fuchsia".into()
}

/// The parameters of a VBMeta descriptor to add to a VBMeta image.
#[derive(Serialize, Deserialize, Debug)]
pub struct VBMetaDescriptor {
    /// Name of the partition.
    pub name: String,

    /// Size of the partition in bytes.
    pub size: u64,

    /// Custom VBMeta flags to add.
    pub flags: u32,

    /// Minimum AVB version to add.
    pub min_avb_version: String,
}

/// The parameters describing how to create a FVM image.
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Fvm {
    /// The size of a slice within the FVM.
    #[serde(default = "default_fvm_slice_size")]
    pub slice_size: u64,

    /// The list of filesystems to generate that can be added to the outputs.
    pub filesystems: Vec<FvmFilesystem>,

    /// The FVM images to generate.
    pub outputs: Vec<FvmOutput>,
}

fn default_fvm_slice_size() -> u64 {
    8388608
}

/// A single FVM filesystem that can be added to multiple outputs.
#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(tag = "type")]
pub enum FvmFilesystem {
    /// A blobfs volume for holding blobs.
    #[serde(rename = "blobfs")]
    BlobFS(BlobFS),

    /// A minfs volume for holding data.
    #[serde(rename = "minfs")]
    MinFS(MinFS),

    /// An empty minfs volume.
    /// This is often used to reserve the minfs volume, but wait until boot-time to format the
    /// partition.
    #[serde(rename = "empty-minfs")]
    EmptyMinFS(EmptyMinFS),

    /// An empty account volume.
    /// This volume is used to hold user data.
    #[serde(rename = "empty-account")]
    EmptyAccount(EmptyAccount),

    /// Reserved slices in the FVM.
    #[serde(rename = "reserved")]
    Reserved(Reserved),
}

/// Configuration for building a BlobFS volume.
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct BlobFS {
    /// The name of the volume in the FVM.
    #[serde(default = "default_blobfs_name")]
    pub name: String,

    /// Optionally compress the volume file.
    #[serde(default = "default_blobfs_compress")]
    pub compress: bool,

    /// Optional deprecated layout.
    #[serde(default = "default_blobfs_layout")]
    pub layout: BlobFSLayout,

    /// Reserve |minimum_data_bytes| and |minimum_inodes| in the FVM, and ensure
    /// that the final reserved size does not exceed |maximum_bytes|.
    #[serde(default)]
    pub maximum_bytes: Option<u64>,

    /// Reserve space for at least this many data bytes.
    #[serde(default)]
    pub minimum_data_bytes: Option<u64>,

    /// Reserved space for this many inodes.
    #[serde(default)]
    pub minimum_inodes: Option<u64>,

    /// Maximum amount of contents for an assembled blobfs.
    #[serde(default)]
    pub maximum_contents_size: Option<u64>,
}

fn default_blobfs_name() -> String {
    "blob".into()
}

fn default_blobfs_compress() -> bool {
    true
}

fn default_blobfs_layout() -> BlobFSLayout {
    BlobFSLayout::Compact
}

/// Configuration for building a MinFS volume.
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct MinFS {
    /// The name of the volume in the FVM.
    #[serde(default = "default_minfs_name")]
    pub name: String,

    /// Reserve |minimum_data_bytes| and |minimum_inodes| in the FVM, and ensure
    /// that the final reserved size does not exceed |maximum_bytes|.
    #[serde(default)]
    pub maximum_bytes: Option<u64>,

    /// Reserve space for at least this many data bytes.
    #[serde(default)]
    pub minimum_data_bytes: Option<u64>,

    /// Reserved space for this many inodes.
    #[serde(default)]
    pub minimum_inodes: Option<u64>,
}

fn default_minfs_name() -> String {
    "data".into()
}

/// Configuration for building a EmptyMinFS volume.
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct EmptyMinFS {
    /// The name of the volume in the FVM.
    #[serde(default = "default_minfs_name")]
    pub name: String,
}

fn default_account_name() -> String {
    "account".into()
}

/// Configuration for building a EmptyAccount volume.
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct EmptyAccount {
    /// The name of the volume in the FVM.
    #[serde(default = "default_account_name")]
    pub name: String,
}

/// Configuration for building a Reserved volume.
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Reserved {
    /// The name of the volume in the FVM.
    #[serde(default = "default_reserved_name")]
    pub name: String,

    /// The number of slices to reserve.
    pub slices: u64,
}

fn default_reserved_name() -> String {
    "internal".into()
}

/// The internal layout of blobfs.
#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
pub enum BlobFSLayout {
    /// A more compact layout than DeprecatedPadded.
    #[serde(rename = "compact")]
    Compact,

    /// A layout that is deprecated, but kept for compatibility reasons.
    #[serde(rename = "deprecated_padded")]
    DeprecatedPadded,
}

impl FromStr for BlobFSLayout {
    type Err = anyhow::Error;
    fn from_str(s: &str) -> Result<Self> {
        blobfs_layout_from_str(s)
    }
}

impl TryFrom<&str> for BlobFSLayout {
    type Error = anyhow::Error;
    fn try_from(s: &str) -> Result<Self> {
        blobfs_layout_from_str(s)
    }
}

impl fmt::Display for BlobFSLayout {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}",
            match self {
                BlobFSLayout::Compact => "compact",
                BlobFSLayout::DeprecatedPadded => "deprecated_padded",
            }
        )
    }
}

fn blobfs_layout_from_str(s: &str) -> Result<BlobFSLayout> {
    match s {
        "compact" => Ok(BlobFSLayout::Compact),
        "deprecated_padded" => Ok(BlobFSLayout::DeprecatedPadded),
        _ => Err(anyhow!("invalid blobfs layout")),
    }
}

/// A FVM image to generate with a list of filesystems.
#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(tag = "type")]
pub enum FvmOutput {
    /// The default FVM type with no modifications.
    #[serde(rename = "standard")]
    Standard(StandardFvm),

    /// A FVM that is compressed sparse.
    #[serde(rename = "sparse")]
    Sparse(SparseFvm),

    /// A FVM prepared for a Nand partition.
    #[serde(rename = "nand")]
    Nand(NandFvm),
}

/// The default FVM type with no modifications.
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct StandardFvm {
    /// The name to give the file.
    pub name: String,

    /// The filesystems to include in the FVM.
    pub filesystems: Vec<String>,

    /// Whether to compress the FVM.
    #[serde(default)]
    pub compress: bool,

    /// Shrink the FVM to fit exactly the contents.
    #[serde(default)]
    pub resize_image_file_to_fit: bool,

    /// After the optional resize, truncate the file to this length.
    #[serde(default)]
    pub truncate_to_length: Option<u64>,
}

/// A FVM that is compressed sparse.
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct SparseFvm {
    /// The name to give the file.
    pub name: String,

    /// The filesystems to include in the FVM.
    pub filesystems: Vec<String>,

    /// The maximum size the FVM can expand to at runtime.
    /// This sets the amount of slice metadata to allocate during construction,
    /// which cannot be modified at runtime.
    #[serde(default)]
    pub max_disk_size: Option<u64>,
}

/// A FVM prepared for a Nand partition.
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct NandFvm {
    /// The name to give the file.
    pub name: String,

    /// The filesystems to include in the FVM.
    #[serde(default)]
    pub filesystems: Vec<String>,

    /// The maximum size the FVM can expand to at runtime.
    /// This sets the amount of slice metadata to allocate during construction,
    /// which cannot be modified at runtime.
    #[serde(default)]
    pub max_disk_size: Option<u64>,

    /// Whether to compress the FVM.
    #[serde(default)]
    pub compress: bool,

    /// The number of blocks.
    pub block_count: u64,

    /// The out of bound size.
    pub oob_size: u64,

    /// Page size as perceived by the FTL.
    pub page_size: u64,

    /// Number of pages per erase block unit.
    pub pages_per_block: u64,
}

impl ImagesConfig {
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

#[cfg(test)]
mod tests {
    use super::*;
    use std::convert::TryInto;

    #[test]
    fn zbi_compression_try_from() {
        assert_eq!(ZbiCompression::ZStd, "zstd".try_into().unwrap());
        assert_eq!(ZbiCompression::ZStdMax, "zstd.max".try_into().unwrap());
        let compression: Result<ZbiCompression> = "else".try_into();
        assert!(compression.is_err());
    }

    #[test]
    fn zbi_compression_from_string() {
        assert_eq!(ZbiCompression::ZStd, ZbiCompression::from_str("zstd").unwrap());
        assert_eq!(ZbiCompression::ZStdMax, ZbiCompression::from_str("zstd.max").unwrap());
        let compression: Result<ZbiCompression> = ZbiCompression::from_str("else");
        assert!(compression.is_err());
    }

    #[test]
    fn zbi_compressoin_to_string() {
        assert_eq!("zstd".to_string(), ZbiCompression::ZStd.to_string());
        assert_eq!("zstd.max".to_string(), ZbiCompression::ZStdMax.to_string());
    }

    #[test]
    fn blobfs_layout_try_from() {
        assert_eq!(BlobFSLayout::Compact, "compact".try_into().unwrap());
        assert_eq!(BlobFSLayout::DeprecatedPadded, "deprecated_padded".try_into().unwrap());
        let layout: Result<BlobFSLayout> = "else".try_into();
        assert!(layout.is_err());
    }

    #[test]
    fn blobfs_layout_from_string() {
        assert_eq!(BlobFSLayout::Compact, BlobFSLayout::from_str("compact").unwrap());
        assert_eq!(
            BlobFSLayout::DeprecatedPadded,
            BlobFSLayout::from_str("deprecated_padded").unwrap()
        );
        let layout: Result<BlobFSLayout> = BlobFSLayout::from_str("else");
        assert!(layout.is_err());
    }

    #[test]
    fn blobfs_layout_to_string() {
        assert_eq!("compact".to_string(), BlobFSLayout::Compact.to_string());
        assert_eq!("deprecated_padded".to_string(), BlobFSLayout::DeprecatedPadded.to_string());
    }

    #[test]
    fn from_json() {
        let json = r#"
            {
                images: [
                    {
                        type: "zbi",
                        name: "fuchsia",
                        compression: "zstd.max",
                        postprocessing_script: {
                            path: "path/to/tool.sh",
                            args: [ "arg1", "arg2" ]
                        }
                    },
                    {
                        type: "vbmeta",
                        name: "fuchsia",
                        key: "path/to/key",
                        key_metadata: "path/to/key/metadata",
                        additional_descriptors: [
                            {
                                name: "zircon",
                                size: 12345,
                                flags: 1,
                                min_avb_version: "1.1"
                            }
                        ]
                    },
                    {
                        type: "fvm",
                        slice_size: 0,
                        filesystems: [
                            {
                                type: "blobfs",
                                name: "blob",
                                compress: true,
                                layout: "compact",
                                maximum_bytes: 0,
                                minimum_data_bytes: 0,
                                minimum_inodes: 0,
                            },
                            {
                                type: "minfs",
                                name: "data",
                                maximum_bytes: 0,
                                minimum_data_bytes: 0,
                                minimum_inodes: 0,
                            },
                            {
                                type: "reserved",
                                name: "internal",
                                slices: 0,
                            },
                        ],
                        outputs: [
                            {
                                type: "standard",
                                name: "fvm",
                                filesystems: [
                                    "blob",
                                    "data",
                                    "internal",
                                ],
                                resize_image_file_to_fit: true,
                                truncate_to_length: 0,
                            },
                            {
                                type: "sparse",
                                name: "fvm.sparse",
                                filesystems: [
                                    "blob",
                                    "data",
                                    "internal",
                                ],
                                max_disk_size: 0,
                            },
                            {
                                type: "nand",
                                name: "fvm.nand",
                                filesystems: [
                                    "blob",
                                    "data",
                                    "internal",
                                ],
                                compress: true,
                                max_disk_size: 0,
                                block_count: 0,
                                oob_size: 0,
                                page_size: 0,
                                pages_per_block: 0,
                            },
                        ],
                    },
                ],
            }
        "#;
        let mut cursor = std::io::Cursor::new(json);
        let config: ImagesConfig = ImagesConfig::from_reader(&mut cursor).unwrap();
        assert_eq!(config.images.len(), 3);

        let mut found_zbi = false;
        let mut found_vbmeta = false;
        let mut found_standard_fvm = false;
        for image in config.images {
            match image {
                Image::Zbi(zbi) => {
                    found_zbi = true;
                    assert_eq!(zbi.name, "fuchsia");
                    assert!(matches!(zbi.compression, ZbiCompression::ZStdMax));
                }
                Image::VBMeta(vbmeta) => {
                    found_vbmeta = true;
                    assert_eq!(vbmeta.name, "fuchsia");
                    assert_eq!(vbmeta.key, PathBuf::from("path/to/key"));
                }
                Image::Fvm(fvm) => {
                    assert_eq!(fvm.outputs.len(), 3);

                    for output in fvm.outputs {
                        match output {
                            FvmOutput::Standard(standard) => {
                                found_standard_fvm = true;
                                assert_eq!(standard.name, "fvm");
                                assert_eq!(standard.filesystems.len(), 3);
                            }
                            _ => {}
                        }
                    }
                }
            }
        }
        assert!(found_zbi);
        assert!(found_vbmeta);
        assert!(found_standard_fvm);
    }

    #[test]
    fn using_defaults() {
        let json = r#"
            {
                images: [
                    {
                        type: "zbi",
                    },
                    {
                        type: "vbmeta",
                        key: "path/to/key",
                        key_metadata: "path/to/key/metadata",
                    },
                    {
                        type: "fvm",
                        filesystems: [
                            {
                                type: "blobfs",
                                name: "blob",
                            },
                            {
                                type: "minfs",
                                name: "data",
                            },
                            {
                                type: "reserved",
                                name: "internal",
                                slices: 0,
                            },
                        ],
                        outputs: [
                            {
                                type: "standard",
                                name: "fvm.blk",
                                filesystems: [
                                    "blob",
                                    "data",
                                    "internal",
                                ],
                            },
                            {
                                type: "sparse",
                                name: "fvm.sparse.blk",
                                filesystems: [
                                    "blob",
                                    "data",
                                    "internal",
                                ],
                            },
                            {
                                type: "nand",
                                name: "fvm.fastboot.blk",
                                filesystems: [
                                    "blob",
                                    "data",
                                    "internal",
                                ],
                                block_count: 0,
                                oob_size: 0,
                                page_size: 0,
                                pages_per_block: 0,
                            },
                        ],
                    },
                ],
            }
        "#;
        let mut cursor = std::io::Cursor::new(json);
        let config: ImagesConfig = ImagesConfig::from_reader(&mut cursor).unwrap();
        assert_eq!(config.images.len(), 3);
    }
}
