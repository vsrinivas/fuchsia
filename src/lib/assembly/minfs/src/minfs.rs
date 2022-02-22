// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use assembly_tool::Tool;
use assembly_util::PathToStringExt;
use std::path::Path;

/// Builder for MinFS. Currently, it only writes an empty minfs.
pub struct MinFSBuilder {
    /// Path to the minfs host tool.
    tool: Box<dyn Tool>,
}

impl MinFSBuilder {
    /// Construct a new MinFSBuilder that uses the minfs |tool|.
    pub fn new(tool: Box<dyn Tool>) -> Self {
        Self { tool }
    }

    /// Build minfs and write the file to `output`.
    pub fn build(self, output: impl AsRef<Path>) -> Result<()> {
        let minfs_args = self.build_args(output)?;
        self.tool.run(&minfs_args)
    }

    /// Get the build arguments to pass to the minfs tool.
    fn build_args(&self, output: impl AsRef<Path>) -> Result<Vec<String>> {
        Ok(vec![output.as_ref().path_to_string()?, "create".to_string()])
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assembly_tool::testing::FakeToolProvider;
    use assembly_tool::{ToolCommandLog, ToolProvider};
    use serde_json::json;
    use std::path::PathBuf;

    #[test]
    fn build_args() {
        let tools = FakeToolProvider::default();
        let minfs_tool = tools.get_tool("minfs").unwrap();
        let builder = MinFSBuilder::new(minfs_tool);

        let args = builder.build_args("minfs").unwrap();
        assert_eq!(args, vec!["minfs".to_string(), "create".to_string()]);

        let args = builder.build_args("minfs2").unwrap();
        assert_eq!(args, vec!["minfs2".to_string(), "create".to_string()]);
    }

    #[test]
    fn build() {
        let tools = FakeToolProvider::default();
        let minfs_tool = tools.get_tool("minfs").unwrap();
        let builder = MinFSBuilder::new(minfs_tool);
        let minfs_path: PathBuf = "data.blk".into();
        builder.build(&minfs_path).unwrap();

        // Ensure the command was run correctly.
        let expected_commands: ToolCommandLog = serde_json::from_value(json!({
            "commands": [
                {
                    "tool": "./host_x64/minfs",
                    "args": [
                        "data.blk",
                        "create"
                    ]
                }
            ]
        }))
        .unwrap();
        assert_eq!(&expected_commands, tools.log());
    }
}
