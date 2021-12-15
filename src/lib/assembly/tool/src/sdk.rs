// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{Tool, ToolCommand, ToolCommandLog, ToolProvider};

use anyhow::{Context, Result};
use assembly_util::PathToStringExt;
use ffx_config::{get_sdk, sdk::Sdk};
use futures::executor::block_on;
use std::path::PathBuf;
use std::process::Command;

/// Implementation of ToolProvider that fetches tools from the SDK.
pub struct SdkToolProvider {
    /// The SDK object that can find the tools based on a manifest.
    sdk: Sdk,

    /// The log of the commands run.
    log: ToolCommandLog,
}

impl SdkToolProvider {
    /// Attempt to create a new SdkToolProvider. This will return an Err if the manifest cannot be
    /// found, parsed, or is invalid.
    pub fn try_new() -> Result<Self> {
        Ok(Self { sdk: block_on(get_sdk())?, log: ToolCommandLog::default() })
    }
}

impl ToolProvider for SdkToolProvider {
    fn get_tool(&self, name: impl AsRef<str>) -> Result<Box<dyn Tool>> {
        let path = self.sdk.get_host_tool(name.as_ref())?;
        let tool = SdkTool::new(path, self.log.clone());
        Ok(Box::new(tool))
    }

    fn log(&self) -> &ToolCommandLog {
        &self.log
    }
}

/// A tool in the SDK that can be executed with a list of arguments.
#[derive(Debug)]
struct SdkTool {
    /// Path to the tool.
    path: PathBuf,

    /// A reference to the log inside the parent SdkToolProvider. When `run()` is called, the tool
    /// appends to the `log`.
    log: ToolCommandLog,
}

impl SdkTool {
    /// Construct a SdkTool.
    fn new(path: PathBuf, log: ToolCommandLog) -> Self {
        Self { path, log }
    }
}

impl Tool for SdkTool {
    fn run(&self, args: &[String]) -> Result<()> {
        let path = self.path.path_to_string()?;
        self.log.add(ToolCommand::new(path.clone(), args.into()));
        let output = Command::new(&self.path)
            .args(args)
            .output()
            .context(format!("Failed to run the tool: {}", path))?;
        if !output.status.success() {
            anyhow::bail!(format!("{} exited with status: {}", path, output.status));
        }
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use crate::tool::{Tool, ToolCommandLog};

    use super::SdkTool;
    use assembly_test_util::generate_fake_tool;
    use tempfile::tempdir;

    #[test]
    fn test_sdk_tool() {
        // Generate a fake tool script that fails unless "pass" is the first argument.
        let dir = tempdir().unwrap();
        let tool_path = dir.path().join("tool.sh");
        generate_fake_tool(
            &tool_path,
            r#"#!/bin/bash
               if [[ "$1" != "pass" ]]; then
                 exit 1
               fi
               exit 0
           "#,
        );

        // Create an SdkTool with a log.
        let log = ToolCommandLog::default();
        let tool = SdkTool::new(tool_path, log);

        // Test a few scenarios.
        assert!(tool.run(&[]).is_err());
        assert!(tool.run(&["fail".into()]).is_err());
        assert!(tool.run(&["pass".into()]).is_ok());
    }
}
