// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{Ffx, FfxCommandLine};
use anyhow::Result;
use argh::EarlyExit;
use async_trait::async_trait;
use ffx_config::EnvironmentContext;

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
    fn command_list(&self) -> &[&argh::CommandInfo] {
        Self::global_command_list()
    }

    /// Parses the given command line information into a runnable command
    /// object.
    fn try_from_args(
        &self,
        cmd: &FfxCommandLine,
        args: &[&str],
    ) -> Result<Option<Box<dyn ToolRunner>>, EarlyExit>;

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
}
