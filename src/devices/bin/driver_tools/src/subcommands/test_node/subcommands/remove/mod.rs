// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    anyhow::anyhow, anyhow::Result, args::RemoveTestNodeCommand,
    fidl_fuchsia_driver_development as fdd,
};

pub async fn remove_test_node(
    cmd: &RemoveTestNodeCommand,
    driver_development_proxy: fdd::DriverDevelopmentProxy,
) -> Result<()> {
    driver_development_proxy
        .remove_test_node(&cmd.name)
        .await?
        .map_err(|e| anyhow!("Calling RemoveTestNode failed with {:#?}", e))?;
    Ok(())
}
