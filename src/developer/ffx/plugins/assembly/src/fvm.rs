// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config::{FvmConfig, FvmFilesystemEntry};

use anyhow::Result;
use assembly_fvm::{Filesystem, FvmBuilder};
use std::path::{Path, PathBuf};

pub fn construct_fvm(
    outdir: impl AsRef<Path>,
    fvm_config: &FvmConfig,
    blobfs_path: Option<impl AsRef<Path>>,
) -> Result<PathBuf> {
    // Create the builder.
    let fvm_path = outdir.as_ref().join("fvm.blk");
    let mut fvm_builder =
        FvmBuilder::new(&fvm_path, fvm_config.slice_size, fvm_config.reserved_slices);

    // Add all the filesystems.
    for entry in &fvm_config.filesystems {
        let (path, attributes) = match entry {
            FvmFilesystemEntry::BlobFS { attributes } => {
                let path = match &blobfs_path {
                    Some(path) => path.as_ref(),
                    None => {
                        anyhow::bail!("BlobFS configuration exists, but BlobFS was not generated");
                    }
                };
                (path.to_path_buf(), attributes.clone())
            }
            FvmFilesystemEntry::MinFS { path, attributes } => {
                (path.to_path_buf(), attributes.clone())
            }
        };
        fvm_builder.filesystem(Filesystem { path, attributes });
    }

    // Construct the FVM.
    fvm_builder.build()?;
    Ok(fvm_path)
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::config::FvmConfig;
    use tempfile::tempdir;

    #[test]
    fn construct() {
        let dir = tempdir().unwrap();
        let fvm_config = FvmConfig {
            partition: "fvm".to_string(),
            slice_size: 8388608,
            reserved_slices: 1,
            filesystems: Vec::new(),
        };
        let fvm_path =
            construct_fvm(dir.path(), &fvm_config, Some(dir.path().join("blob.blk"))).unwrap();
        assert_eq!(fvm_path, dir.path().join("fvm.blk"));
    }
}
