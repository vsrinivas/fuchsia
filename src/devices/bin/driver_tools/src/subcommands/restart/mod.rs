// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    super::common,
    anyhow::{format_err, Result},
    args::RestartCommand,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol, fidl_fuchsia_driver_development as fdd,
};

pub async fn restart(
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: RestartCommand,
) -> Result<()> {
    let driver_dev_proxy = common::get_development_proxy(remote_control, cmd.select).await?;
    register_impl(driver_dev_proxy, cmd, &mut std::io::stdout()).await
}

pub async fn register_impl<W: std::io::Write>(
    driver_dev_proxy: fdd::DriverDevelopmentProxy,
    cmd: RestartCommand,
    writer: &mut W,
) -> Result<()> {
    writeln!(writer, "Restarting driver hosts containing {}", cmd.driver_path)?;
    match driver_dev_proxy.restart_driver_hosts(&mut cmd.driver_path.to_string()).await? {
        Ok(n) => {
            if n == 0 {
                writeln!(
                    writer,
                    "Did not find any matching driver hosts. Is the driver running and listed by `ffx driver list --loaded`?"
                )?;
            } else {
                writeln!(writer, "Restarted {} driver host{}.", n, if n == 1 { "" } else { "s" })?;
            }
            Ok(())
        }
        Err(err) => Err(format_err!("{:?}", err)),
    }
}
