// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use assembly_util::PathToStringExt;
use std::path::{Path, PathBuf};

/// Builder for MinFS. Currently, it only writes an empty minfs.
pub struct MinFSBuilder {
    /// Path to the minfs host tool.
    tool: PathBuf,
}

impl MinFSBuilder {
    /// Construct a new MinFSBuilder that uses the minfs |tool|.
    pub fn new(tool: impl AsRef<Path>) -> Self {
        Self { tool: tool.as_ref().to_path_buf() }
    }

    /// Build minfs and write the file to `output`.
    pub fn build(self, output: impl AsRef<Path>) -> Result<()> {
        let minfs_args = self.build_args(output)?;
        let output = std::process::Command::new(&self.tool).args(&minfs_args).output();
        let output = output.context("Failed to run the minfs tool")?;
        if !output.status.success() {
            anyhow::bail!(format!("Failed to generate minfs with output: {:?}", output));
        }

        Ok(())
    }

    /// Get the build arguments to pass to the minfs tool.
    fn build_args(&self, output: impl AsRef<Path>) -> Result<Vec<String>> {
        Ok(vec![output.as_ref().path_to_string()?, "create".to_string()])
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assembly_test_util::generate_fake_tool_nop;
    use serial_test::serial;
    use tempfile::TempDir;

    // These tests must be ran serially, because otherwise they will affect each
    // other through process spawming. If a test spawns a process while the
    // other test has an open file, then the spawned process will get a copy of
    // the open file descriptor, preventing the other test from executing it.
    #[test]
    #[serial]
    fn build_args() {
        let dir = TempDir::new().unwrap();
        let tool_path = dir.path().join("minfs.sh");
        generate_fake_tool_nop(&tool_path);

        let builder = MinFSBuilder::new(&tool_path);

        let args = builder.build_args("minfs").unwrap();
        assert_eq!(args, vec!["minfs".to_string(), "create".to_string()]);

        let args = builder.build_args("minfs2").unwrap();
        assert_eq!(args, vec!["minfs2".to_string(), "create".to_string()]);
    }

    #[test]
    #[serial]
    fn build() {
        let dir = TempDir::new().unwrap();
        let tool_path = dir.path().join("minfs.sh");
        generate_fake_tool_nop(&tool_path);

        let builder = MinFSBuilder::new(&tool_path);

        let minfs_path = dir.as_ref().join("data.blk");
        builder.build(&minfs_path).unwrap();
    }
}
