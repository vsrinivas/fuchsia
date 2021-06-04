// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_emulator::vdl_files::VDLFiles;
use ffx_emulator_args::{Args, VDLCommand};

fn main() -> Result<()> {
    let Args { command, sdk } = argh::from_env();
    process_command(command, sdk)
}

fn process_command(command: VDLCommand, is_sdk: bool) -> Result<()> {
    match command {
        VDLCommand::Start(start_command) => std::process::exit(
            VDLFiles::new(is_sdk, start_command.verbose)?.start_emulator(&start_command)?,
        ),
        VDLCommand::Kill(stop_command) => {
            VDLFiles::new(is_sdk, false)?.stop_vdl(&stop_command)?;
        }
        VDLCommand::Remote(remote_command) => {
            VDLFiles::new(is_sdk, false)?.remote_emulator(&remote_command)?;
        }
    }
    Ok(())
}
