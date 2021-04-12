// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    ffx_core::ffx_plugin,
    ffx_starnix_start_args::StartStarnixCommand,
    fidl_fuchsia_starnix_developer::ManagerProxy,
};

#[ffx_plugin(
    "starnix_enabled",
    ManagerProxy = "core/starnix_manager:expose:fuchsia.starnix.developer.Manager"
)]
pub async fn start_starnix(manager_proxy: ManagerProxy, args: StartStarnixCommand) -> Result<()> {
    manager_proxy
        .start(&args.url)
        .await
        .map_err(|e| anyhow!("Error starting component: {:?}", e))?;
    Ok(())
}
