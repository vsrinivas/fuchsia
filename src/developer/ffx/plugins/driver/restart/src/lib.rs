// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    ffx_core::ffx_plugin,
    ffx_driver_restart_args::DriverRestartCommand,
    fidl_fuchsia_driver_development::DriverDevelopmentProxy,
};

#[ffx_plugin(
    "driver_enabled",
    DriverDevelopmentProxy = "bootstrap/driver_manager:expose:fuchsia.driver.development.DriverDevelopment"
)]
pub async fn register(
    driver_dev_proxy: DriverDevelopmentProxy,
    cmd: DriverRestartCommand,
) -> Result<()> {
    println!("Restarting driver hosts containing {}", cmd.driver_path);
    driver_dev_proxy
        .restart_driver_hosts(&mut cmd.driver_path.to_string())
        .await?
        .map_err(|err| format_err!("{:?}", err))
}
