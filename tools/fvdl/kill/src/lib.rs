// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_developer_bridge as bridge;
use fvdl_emulator_common::vdl_files::VDLFiles;
use fvdl_emulator_kill_args::KillCommand;

pub async fn kill(
    cmd: KillCommand,
    daemon_proxy: bridge::DaemonProxy,
) -> Result<(), anyhow::Error> {
    VDLFiles::new(cmd.sdk, false)?.stop_vdl(&cmd, Some(&daemon_proxy)).await
}
