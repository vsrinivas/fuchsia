// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    std::process::Command,
};

pub static SYSTEM_COMMAND_RUNNER: CommandRunner = system_run_command;

pub struct ExitStatus(pub i32);

impl ExitStatus {
    pub fn success(&self) -> bool {
        self.0 == 0
    }
}

/// Describes a function that runs a command with args and returns:
/// exit status, stdout, stderr.
pub type CommandRunner = fn(&Vec<&str>) -> Result<(ExitStatus, String, String), anyhow::Error>;

/// Runs `args` as a command using std::process::Command.
pub fn system_run_command(args: &Vec<&str>) -> Result<(ExitStatus, String, String)> {
    let output = Command::new(&args[0])
        .args(&args[1..])
        .output()
        .context(format!("Could not run '{}'", args.join(" ")))?;
    Ok((
        ExitStatus(output.status.code().ok_or_else(|| anyhow!("No exit code from command"))?),
        String::from_utf8(output.stdout)?,
        String::from_utf8(output.stderr)?,
    ))
}
