// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::FfxError,
    ffx_core::ffx_plugin,
    ffx_wait_args::WaitCommand,
    fidl_fuchsia_developer_bridge::{DaemonError, TargetProxy},
    std::time::Duration,
    timeout::timeout,
};

#[ffx_plugin()]
pub async fn get_ssh_address(target_proxy: TargetProxy, cmd: WaitCommand) -> Result<()> {
    wait_for_device(target_proxy, cmd).await
}

async fn wait_for_device(target_proxy: TargetProxy, cmd: WaitCommand) -> Result<()> {
    let mut elapsed_seconds = 0;
    let ffx: ffx_lib_args::Ffx = argh::from_env();
    let is_default_target = ffx.target.is_none();

    // TODO(fxbug.dev/78946) Remove the loop when we can get the target's address even if we run
    // get_ssh_address() before the target is initialized or connected.
    while elapsed_seconds < cmd.timeout {
        let result = timeout(Duration::from_secs(1), target_proxy.get_ssh_address()).await;
        match result {
            Ok(_) => break,
            Err(_) => {
                if elapsed_seconds + 1 < cmd.timeout {
                    elapsed_seconds += 1;
                } else {
                    result.map_err(|_timeout_err| FfxError::DaemonError {
                        err: DaemonError::Timeout,
                        target: ffx.target.clone(),
                        is_default_target,
                    })??;
                }
            }
        };
    }

    Ok(())
}
