// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::pbm::update_cmd_with_pbm;
use ffx_core::ffx_plugin;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_engines_vdl::VDLFiles;
use ffx_emulator_start_args::StartCommand;
use fidl_fuchsia_developer_bridge as bridge;

mod pbm;

#[ffx_plugin("emu.experimental")]
pub async fn start(
    cmd: StartCommand,
    daemon_proxy: bridge::DaemonProxy,
) -> Result<(), anyhow::Error> {
    let cmd = update_cmd_with_pbm(cmd).await?;

    let config = FfxConfigWrapper::new();
    std::process::exit(
        VDLFiles::new(cmd.sdk, cmd.verbose, &config)
            .await?
            .start_emulator(&cmd, Some(&daemon_proxy))
            .await?,
    )
}
