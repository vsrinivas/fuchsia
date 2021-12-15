// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::serde_arc;

use anyhow::Result;
use serde::{Deserialize, Serialize};
use std::cell::RefCell;
use std::sync::Arc;

/// A producer of `Tool`s that can be run, and log their execution.
pub trait ToolProvider {
    /// Access a tool from the provider.
    fn get_tool(&self, name: impl AsRef<str>) -> Result<Box<dyn Tool>>;

    /// Get the log of the commands that have been run.
    fn log(&self) -> &ToolCommandLog;
}

/// A single tool that can be run.
pub trait Tool {
    /// Run the tool with the |args|.
    fn run(&self, args: &[String]) -> Result<()>;
}

/// Log that holds the commands run for several tools.
#[derive(Deserialize, Serialize, Clone, Debug, Default, PartialEq)]
pub struct ToolCommandLog {
    /// The list of commands that were run.
    #[serde(with = "serde_arc")]
    commands: Arc<RefCell<Vec<ToolCommand>>>,
}

impl ToolCommandLog {
    /// Add a command to the log.
    pub fn add(&self, command: ToolCommand) {
        self.commands.borrow_mut().push(command);
    }
}

/// A single command, representing the execution of a `Tool`.
#[derive(Deserialize, Serialize, Debug, Default, PartialEq)]
pub struct ToolCommand {
    /// The tool's name.
    tool: String,

    /// The arguments passed to the tool.
    args: Vec<String>,
}

impl ToolCommand {
    /// Construct a new ToolCommand.
    pub fn new(tool: String, args: Vec<String>) -> Self {
        Self { tool, args }
    }
}
