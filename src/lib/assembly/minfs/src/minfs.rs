// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use assembly_util::PathToStringExt;
use std::path::Path;

/// Builder for MinFS. Currently, it only writes an empty minfs.
pub struct MinFSBuilder {}
impl MinFSBuilder {
    /// Build minfs and write the file to `output`.
    pub fn build(output: impl AsRef<Path>) -> Result<()> {
        let minfs_args = Self::build_args(output)?;

        // TODO(fxbug.dev/76378): Take the tool location from a config.
        let output = std::process::Command::new("host_x64/minfs").args(&minfs_args).output();
        let output = output.context("Failed to run the minfs tool")?;
        if !output.status.success() {
            anyhow::bail!(format!("Failed to generate minfs with output: {:?}", output));
        }

        Ok(())
    }

    /// Get the build arguments to pass to the minfs tool.
    fn build_args(output: impl AsRef<Path>) -> Result<Vec<String>> {
        Ok(vec![output.as_ref().path_to_string()?, "create".to_string()])
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn build_args() {
        let args = MinFSBuilder::build_args("minfs").unwrap();
        assert_eq!(args, vec!["minfs".to_string(), "create".to_string()]);

        let args = MinFSBuilder::build_args("minfs2").unwrap();
        assert_eq!(args, vec!["minfs2".to_string(), "create".to_string()]);
    }

    #[test]
    fn build() {
        let outdir = TempDir::new().unwrap();
        let minfs_path = outdir.as_ref().join("data.blk");
        MinFSBuilder::build(&minfs_path).unwrap();
        assert!(minfs_path.exists())
    }
}
