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
    register_impl(driver_dev_proxy, cmd, &mut std::io::stdout()).await
}

pub async fn register_impl<W: std::io::Write>(
    driver_dev_proxy: DriverDevelopmentProxy,
    cmd: DriverRestartCommand,
    writer: &mut W,
) -> Result<()> {
    writeln!(writer, "Restarting driver hosts containing {}", cmd.driver_path)?;
    match driver_dev_proxy.restart_driver_hosts(&mut cmd.driver_path.to_string()).await? {
        Ok(n) => {
            if n == 0 {
                // TODO(fxbug.dev/89105): Suggest `ffx driver list --loaded` once that flag is
                // implemented.
                writeln!(
                    writer,
                    "Did not find any matching driver hosts. Is the driver running?"
                )?;
            } else {
                writeln!(writer, "Restarted {} driver host{}.", n, if n == 1 { "" } else { "s" })?;
            }
            Ok(())
        }
        Err(err) => Err(format_err!("{:?}", err)),
    }
}
