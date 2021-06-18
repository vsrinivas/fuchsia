// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, errors::FfxError, ffx_core::ffx_plugin, ffx_wait_args::WaitCommand,
    fidl_fuchsia_developer_bridge::DaemonProxy, std::time::Duration,
};

#[ffx_plugin()]
pub async fn get_ssh_address(daemon_proxy: DaemonProxy, cmd: WaitCommand) -> Result<()> {
    wait_for_device(daemon_proxy, cmd).await
}

async fn wait_for_device(daemon_proxy: DaemonProxy, cmd: WaitCommand) -> Result<()> {
    let mut elapsed_seconds = 0;
    let ffx: ffx_lib_args::Ffx = argh::from_env();
    let is_default_target = ffx.target.is_none();

    // TODO(fxbug.dev/78946) Remove the loop when we can get the target's address even if we run
    // get_ssh_address() before the target is initialized or connected.
    while elapsed_seconds < cmd.timeout {
        let target: Option<String> = ffx_config::get("target.default").await?;
        let result = daemon_proxy
            .get_ssh_address(target.as_deref(), Duration::from_secs(1).as_nanos() as i64)
            .await?;

        match result {
            Ok(_) => break,
            Err(_) => {
                if elapsed_seconds + 1 < cmd.timeout {
                    elapsed_seconds += 1;
                } else {
                    result.map_err(|e| FfxError::DaemonError {
                        err: e,
                        target: target,
                        is_default_target,
                    })?;
                }
            }
        };
    }

    Ok(())
}
