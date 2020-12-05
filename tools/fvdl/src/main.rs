// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::args::{Args, VDLCommand};
use crate::vdl_files::VDLFiles;
use anyhow::Result;

mod args;
mod portpicker;
mod types;
mod vdl_files;

fn main() -> Result<()> {
    let Args { command } = argh::from_env();
    process_command(command)
}

fn process_command(command: VDLCommand) -> Result<()> {
    match command {
        VDLCommand::Start(start_command) => {
            VDLFiles::new()?.start_emulator(&start_command)?;
        }
        VDLCommand::Kill(stop_command) => {
            VDLFiles::new()?.stop_vdl(&stop_command)?;
        }
    }
    Ok(())
}
