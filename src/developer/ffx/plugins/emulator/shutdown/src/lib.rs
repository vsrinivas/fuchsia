// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ffx_core::ffx_plugin;
use ffx_emulator_engines_vdl::{config::FfxConfigWrapper, VDLFiles};
use ffx_emulator_shutdown_args::ShutdownCommand;
use fidl_fuchsia_developer_bridge as bridge;

#[ffx_plugin("emu.experimental")]
pub async fn shutdown(
    cmd: ShutdownCommand,
    daemon_proxy: bridge::DaemonProxy,
) -> Result<(), anyhow::Error> {
    let config = FfxConfigWrapper::new();
    VDLFiles::new(cmd.sdk, false, &config).await?.stop_vdl(&cmd, Some(&daemon_proxy)).await
}
