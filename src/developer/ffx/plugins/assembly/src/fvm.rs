// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config::{FastbootConfig, FvmConfig, FvmFilesystemEntry};

use anyhow::Result;
use assembly_fvm::{Filesystem, FvmBuilder, FvmType, NandFvmBuilder};
use std::path::{Path, PathBuf};

/// The resulting paths to each generated FVM.
pub struct Fvms {
    pub default: PathBuf,
    pub sparse: PathBuf,
    pub sparse_blob: PathBuf,
    pub fastboot: Option<PathBuf>,
}

/// Constructs up-to four FVM files. Calling this function generates
/// a default FVM, a sparse FVM, a sparse blob-only FVM, and optionally a FVM
/// ready for fastboot flashing. This function returns the paths to each
/// generated FVM.
///
/// If the |fvm_config| includes information for an EMMC, then an EMMC-supported
/// sparse FVM will also be generated for fastboot flashing.
///
/// If the |fvm_config| includes information for a NAND, then an NAND-supported
/// sparse FVM will also be generated for fastboot flashing.
pub fn construct_fvm(
    outdir: impl AsRef<Path>,
    fvm_config: &FvmConfig,
    blobfs_path: Option<impl AsRef<Path>>,
) -> Result<Fvms> {
    // Gather blobfs and minfs details.
    let mut blobfs: Option<Filesystem> = None;
    let mut minfs: Option<Filesystem> = None;
    for entry in &fvm_config.filesystems {
        match entry {
            FvmFilesystemEntry::BlobFS { attributes } => {
                let path = match &blobfs_path {
                    Some(path) => path.as_ref(),
                    None => {
                        anyhow::bail!("BlobFS configuration exists, but BlobFS was not generated");
                    }
                };
                blobfs =
                    Some(Filesystem { path: path.to_path_buf(), attributes: attributes.clone() });
            }
            FvmFilesystemEntry::MinFS { path, attributes } => {
                minfs =
                    Some(Filesystem { path: path.to_path_buf(), attributes: attributes.clone() });
            }
        };
    }
    let (blobfs, minfs) = match (blobfs, minfs) {
        (Some(blobfs), Some(minfs)) => (blobfs, minfs),
        _ => anyhow::bail!("Both blobfs and minfs must be supplied in the configuration"),
    };

    // Build a default FVM that is non-sparse.
    let default_path = outdir.as_ref().join("fvm.blk");
    let mut fvm_builder = FvmBuilder::new(
        &default_path,
        fvm_config.slice_size,
        fvm_config.reserved_slices,
        None,
        FvmType::Default,
    );
    fvm_builder.filesystem(blobfs.clone());
    fvm_builder.filesystem(minfs.clone());
    fvm_builder.build()?;

    // Build a sparse FVM for paving.
    let sparse_path = outdir.as_ref().join("fvm.sparse.blk");
    let mut sparse_fvm_builder = FvmBuilder::new(
        &sparse_path,
        fvm_config.slice_size,
        fvm_config.reserved_slices,
        fvm_config.max_disk_size,
        FvmType::Sparse { empty_minfs: false },
    );
    sparse_fvm_builder.filesystem(blobfs.clone());
    sparse_fvm_builder.filesystem(minfs.clone());
    sparse_fvm_builder.build()?;

    // Build a sparse FVM with an empty minfs.
    let sparse_blob_path = outdir.as_ref().join("fvm.blob.sparse.blk");
    let mut sparse_blob_fvm_builder = FvmBuilder::new(
        &sparse_blob_path,
        fvm_config.slice_size,
        fvm_config.reserved_slices,
        fvm_config.max_disk_size,
        FvmType::Sparse { empty_minfs: true },
    );
    sparse_blob_fvm_builder.filesystem(blobfs.clone());
    sparse_blob_fvm_builder.build()?;

    // Build a sparse fastboot-supported FVM if needed.
    let fastboot_path: Option<PathBuf> = match &fvm_config.fastboot {
        // EMMC formatted FVM.
        Some(FastbootConfig::Emmc { compression, length }) => {
            let emmc_path = outdir.as_ref().join("fvm.fastboot.blk");
            let mut emmc_fvm_builder = FvmBuilder::new(
                &emmc_path,
                fvm_config.slice_size,
                fvm_config.reserved_slices,
                fvm_config.max_disk_size,
                FvmType::Emmc { compression: compression.clone(), length: *length },
            );
            emmc_fvm_builder.filesystem(blobfs.clone());
            emmc_fvm_builder.filesystem(minfs.clone());
            emmc_fvm_builder.build()?;
            Some(emmc_path)
        }

        // NAND formatted FVM.
        Some(FastbootConfig::Nand {
            compression,
            page_size,
            oob_size,
            pages_per_block,
            block_count,
        }) => {
            let nand_path = outdir.as_ref().join("fvm.fastboot.blk");
            let nand_fvm_builder = NandFvmBuilder {
                output: nand_path.clone(),
                sparse_blob_fvm: sparse_blob_path.clone(),
                max_disk_size: fvm_config.max_disk_size,
                compression: compression.clone(),
                page_size: *page_size,
                oob_size: *oob_size,
                pages_per_block: *pages_per_block,
                block_count: *block_count,
            };
            nand_fvm_builder.build()?;
            Some(nand_path)
        }

        None => None,
    };

    Ok(Fvms {
        default: default_path,
        sparse: sparse_path,
        sparse_blob: sparse_blob_path,
        fastboot: fastboot_path,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::config::{FastbootConfig, FvmConfig, FvmFilesystemEntry};
    use assembly_fvm::FilesystemAttributes;
    use std::path::PathBuf;
    use tempfile::tempdir;

    #[test]
    fn construct() {
        let dir = tempdir().unwrap();
        let fvm_config = FvmConfig {
            partition: "fvm".to_string(),
            slice_size: 8388608,
            reserved_slices: 1,
            max_disk_size: None,
            fastboot: None,
            filesystems: generate_test_filesystems(),
        };
        let fvms =
            construct_fvm(dir.path(), &fvm_config, Some(dir.path().join("blob.blk"))).unwrap();
        assert_eq!(fvms.default, dir.path().join("fvm.blk"));
        assert_eq!(fvms.sparse, dir.path().join("fvm.sparse.blk"));
        assert_eq!(fvms.sparse_blob, dir.path().join("fvm.blob.sparse.blk"));
    }

    #[test]
    fn construct_with_emmc() {
        let dir = tempdir().unwrap();
        let fvm_config = FvmConfig {
            partition: "fvm".to_string(),
            slice_size: 8388608,
            reserved_slices: 1,
            max_disk_size: None,
            fastboot: Some(FastbootConfig::Emmc {
                compression: "lz4".to_string(),
                length: 10485760, // 10 MiB
            }),
            filesystems: generate_test_filesystems(),
        };
        let fvms =
            construct_fvm(dir.path(), &fvm_config, Some(dir.path().join("blob.blk"))).unwrap();
        assert_eq!(fvms.default, dir.path().join("fvm.blk"));
        assert_eq!(fvms.sparse, dir.path().join("fvm.sparse.blk"));
        assert_eq!(fvms.sparse_blob, dir.path().join("fvm.blob.sparse.blk"));
        let fastboot = fvms.fastboot.unwrap();
        assert_eq!(fastboot, dir.path().join("fvm.fastboot.blk"));
    }

    #[test]
    fn construct_with_nand() {
        let dir = tempdir().unwrap();
        let fvm_config = FvmConfig {
            partition: "fvm".to_string(),
            slice_size: 8388608,
            reserved_slices: 1,
            max_disk_size: None,
            fastboot: Some(FastbootConfig::Nand {
                compression: None,
                page_size: 4096,
                oob_size: 8,
                pages_per_block: 64,
                block_count: 1024,
            }),
            filesystems: generate_test_filesystems(),
        };
        let fvms =
            construct_fvm(dir.path(), &fvm_config, Some(dir.path().join("blob.blk"))).unwrap();
        assert_eq!(fvms.default, dir.path().join("fvm.blk"));
        assert_eq!(fvms.sparse, dir.path().join("fvm.sparse.blk"));
        assert_eq!(fvms.sparse_blob, dir.path().join("fvm.blob.sparse.blk"));
        let fastboot = fvms.fastboot.unwrap();
        assert_eq!(fastboot, dir.path().join("fvm.fastboot.blk"));
    }

    fn generate_test_filesystems() -> Vec<FvmFilesystemEntry> {
        vec![
            FvmFilesystemEntry::BlobFS {
                attributes: FilesystemAttributes {
                    name: "blobfs".to_string(),
                    minimum_inodes: None,
                    minimum_data_bytes: None,
                    maximum_bytes: None,
                },
            },
            FvmFilesystemEntry::MinFS {
                path: PathBuf::from("path/to/data.blk"),
                attributes: FilesystemAttributes {
                    name: "minfs".to_string(),
                    minimum_inodes: None,
                    minimum_data_bytes: None,
                    maximum_bytes: None,
                },
            },
        ]
    }
}
