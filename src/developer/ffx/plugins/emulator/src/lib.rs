// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::vdl_files::VDLFiles,
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_emulator_args::{EmulatorCommand, VDLCommand},
    fidl_fuchsia_developer_bridge as bridge,
};

pub mod portpicker;
pub mod vdl_files;

mod cipd;
mod device;
mod graphic_utils;
mod images;
mod target;
mod tools;
mod types;
mod vdl_proto_parser;

#[ffx_plugin("emu.experimental")]
pub async fn emulator(cmd: EmulatorCommand, daemon_proxy: bridge::DaemonProxy) -> Result<()> {
    match cmd.command {
        VDLCommand::Start(start_command) => std::process::exit(
            VDLFiles::new(cmd.sdk, start_command.verbose)?
                .start_emulator(&start_command, Some(&daemon_proxy))
                .await?,
        ),
        VDLCommand::Kill(stop_command) => {
            VDLFiles::new(cmd.sdk, false)?.stop_vdl(&stop_command, Some(&daemon_proxy)).await?;
        }
        VDLCommand::Remote(remote_command) => {
            VDLFiles::new(cmd.sdk, false)?.remote_emulator(&remote_command)?;
        }
    }
    Ok(())
}
