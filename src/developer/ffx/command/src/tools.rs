// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{fmt::Write, path::PathBuf};

use crate::{Ffx, FfxCommandLine};
use anyhow::Result;
use argh::EarlyExit;
use async_trait::async_trait;
use ffx_config::EnvironmentContext;

/// Where the command was discovered
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FfxToolSource {
    /// built directly into the executable
    BuiltIn,
    /// discovered in a development tree or workspace tree
    Workspace,
    /// discovered in the currently active SDK
    Sdk,
}

/// Information about a tool for use in help output
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct FfxToolInfo {
    /// Where the tool was found
    pub source: FfxToolSource,
    /// The short name of the tool
    pub name: String,
    /// A longer one-line description of the functionality of the tool
    pub description: String,
    /// A path to the executable for the tool, if there is one
    pub path: Option<PathBuf>,
}

impl FfxToolInfo {
    fn write_description(&self, out: &mut String) {
        crate::describe::write_description(out, &self.name, &self.description)
    }
}

impl From<&argh::CommandInfo> for FfxToolInfo {
    fn from(info: &argh::CommandInfo) -> Self {
        let source = FfxToolSource::BuiltIn;
        let name = info.name.to_owned();
        let description = info.description.to_owned();
        let path = None;
        FfxToolInfo { source, name, description, path }
    }
}

#[async_trait(?Send)]
pub trait ToolRunner {
    fn forces_stdout_log(&self) -> bool;
    async fn run(self: Box<Self>) -> Result<(), anyhow::Error>;
}

/// Implements discovering and loading the subtools a particular ffx binary
/// is capable of running.
pub trait ToolSuite: Sized {
    /// Initializes the tool suite from the ffx invocation's environment.
    fn from_env(app: &Ffx, env: &EnvironmentContext) -> Result<Self>;

    /// Lists commands that should be available no matter how and where this tool
    /// is invoked.
    fn global_command_list() -> &'static [&'static argh::CommandInfo];

    /// Lists all commands reachable from the current context. Defaults to just
    /// the same set of commands as in [`Self::global_command_list`].
    fn command_list(&self) -> Vec<FfxToolInfo> {
        Self::global_command_list().iter().copied().map(|cmd| cmd.into()).collect()
    }

    /// Parses the given command line information into a runnable command
    /// object.
    fn try_from_args(
        &self,
        cmd: &FfxCommandLine,
        args: &[&str],
    ) -> Result<Option<Box<dyn ToolRunner>>, EarlyExit>;

    /// Parses the given command line into a command, then returns a redacted string usable in
    /// analytics. See [`FromArgs::redact_arg_values`] for the kind of output to expect.
    fn redact_arg_values(
        &self,
        cmd: &FfxCommandLine,
        args: &[&str],
    ) -> Result<Vec<String>, EarlyExit>;

    /// Parses the given command line information into a runnable command
    /// object, exiting and printing the early exit output if help is requested
    /// or an error occurs.
    fn from_args(&self, cmd: &FfxCommandLine, args: &[&str]) -> Option<Box<dyn ToolRunner>> {
        self.try_from_args(cmd, args).unwrap_or_else(|early_exit| {
            println!("{}", early_exit.output);

            std::process::exit(match early_exit.status {
                Ok(()) => 0,
                Err(()) => 1,
            })
        })
    }

    /// Prints out a list of the commands this suite has available
    fn print_command_list(&self, w: &mut impl Write) -> Result<(), std::fmt::Error> {
        let mut built_in = None;
        let mut workspace = None;
        let mut sdk = None;
        for cmd in &self.command_list() {
            use FfxToolSource::*;
            let kind = match cmd.source {
                BuiltIn => built_in.get_or_insert_with(String::new),
                Workspace => workspace.get_or_insert_with(String::new),
                Sdk => sdk.get_or_insert_with(String::new),
            };
            cmd.write_description(kind);
        }

        if let Some(built_in) = built_in {
            writeln!(w, "Built-in Commands:\n{built_in}\n")?;
        }
        if let Some(workspace) = workspace {
            writeln!(w, "Workspace Commands:\n{workspace}\n")?;
        }
        if let Some(sdk) = sdk {
            writeln!(w, "SDK Commands:\n{sdk}\n")?;
        }
        Ok(())
    }

    /// Finds the given tool by name in the available command list
    fn find_tool_by_name(&self, name: &str) -> Option<FfxToolInfo> {
        self.command_list().into_iter().find(|cmd| cmd.name == name)
    }
}
