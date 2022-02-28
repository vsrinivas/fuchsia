// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A fake for ToolProvider that can be used in tests.

use crate::tool::{Tool, ToolCommand, ToolCommandLog, ToolProvider};
use anyhow::Result;

/// A provider for tools that no-op, but log their execution so it can be asserted in a test.
pub struct FakeToolProvider {
    /// A log of all the tools that were run.
    log: ToolCommandLog,
    /// Function called for each tool invocation.
    side_effect: fn(&str, &[String]) -> (),
}

impl FakeToolProvider {
    /// Creates a tool provider that notifies a function for each invocation.
    pub fn new_with_side_effect(side_effect: fn(&str, &[String]) -> ()) -> FakeToolProvider {
        FakeToolProvider { log: ToolCommandLog::default(), side_effect }
    }
}

impl Default for FakeToolProvider {
    fn default() -> FakeToolProvider {
        fn noop(_: &str, _: &[String]) -> () {}
        FakeToolProvider::new_with_side_effect(noop)
    }
}

impl ToolProvider for FakeToolProvider {
    fn get_tool(&self, name: &str) -> Result<Box<dyn Tool>> {
        Ok(Box::new(FakeTool {
            name: name.to_string(),
            log: self.log().clone(),
            side_effect: self.side_effect,
        }))
    }

    fn log(&self) -> &ToolCommandLog {
        &self.log
    }
}

/// A fake tool that does not execute a command, but only writes it to the |log|.
struct FakeTool {
    /// The name of the tool.
    name: String,
    /// The log to write the execution details into upon a `run()`.
    log: ToolCommandLog,
    /// Function called for each tool invocation.
    side_effect: fn(&str, &[String]) -> (),
}

impl Tool for FakeTool {
    fn run(&self, args: &[String]) -> Result<()> {
        (self.side_effect)(&self.name, args);
        self.log.add(ToolCommand::new(format!("./host_x64/{}", self.name), args.into()));
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::FakeToolProvider;
    use crate::tool::{ToolCommandLog, ToolProvider};
    use serde_json::json;

    #[test]
    fn test_fake_tool_provider() {
        let expected_log: ToolCommandLog = serde_json::from_value(json!({
            "commands": [
                {
                    "tool": "./host_x64/my_tool",
                    "args": [
                        "foo",
                        "bar",
                    ]
                },
                {
                    "tool": "./host_x64/my_other_tool",
                    "args": [
                        "cat"
                    ]
                }

            ]
        }))
        .unwrap();

        let tools = FakeToolProvider::default();

        let tool = tools.get_tool("my_tool").unwrap();
        assert!(tool.run(&["foo".into(), "bar".into()]).is_ok());
        drop(tool);

        let tool = tools.get_tool("my_other_tool").unwrap();
        assert!(tool.run(&["cat".into()]).is_ok());
        drop(tool);

        assert_eq!(&expected_log, tools.log());
    }
}
