// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::vdl_files::VDLFiles,
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_emulator_args::{EmulatorCommand, VDLCommand},
};

mod cipd;
mod graphic_utils;
mod images;
mod portpicker;
mod tools;
mod types;
pub mod vdl_files;
mod vdl_proto_parser;

#[ffx_plugin("vdl.experimental")]
pub fn emulator(cmd: EmulatorCommand) -> Result<()> {
    process_command(cmd.command, cmd.sdk)
}

fn process_command(command: VDLCommand, is_sdk: bool) -> Result<()> {
    match command {
        VDLCommand::Start(start_command) => {
            VDLFiles::new(is_sdk, start_command.verbose)?.start_emulator(&start_command)?;
        }
        VDLCommand::Kill(stop_command) => {
            VDLFiles::new(is_sdk, false)?.stop_vdl(&stop_command)?;
        }
    }
    Ok(())
}
