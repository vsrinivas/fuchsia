// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    ffx_core::ffx_plugin,
    ffx_driver_device_args::*,
    fidl::endpoints::Proxy,
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_io::DirectoryProxy,
    fuchsia_zircon_status as zx,
    std::convert::TryFrom,
};

#[ffx_plugin("driver_enabled", DirectoryProxy = "bootstrap/driver_manager:expose:dev")]
pub async fn device(dev: DirectoryProxy, cmd: DeviceCommand) -> Result<()> {
    match cmd.subcommand {
        DeviceSubCommand::Bind(BindCommand { ref device_path, ref driver_path }) => {
            let device = connect_to_device(dev, device_path)?;
            device.bind(driver_path).await?.map_err(|err| format_err!("{:?}", err))?;
            println!("Bound {} to {}", driver_path, device_path);
        }
        DeviceSubCommand::Unbind(UnbindCommand { ref device_path }) => {
            let device = connect_to_device(dev, device_path)?;
            device.schedule_unbind().await?.map_err(|err| format_err!("{:?}", err))?;
            println!("Unbound driver from {}", device_path);
        }
        DeviceSubCommand::LogLevel(LogLevelCommand { ref device_path, log_level }) => {
            let device = connect_to_device(dev, device_path)?;
            if let Some(log_level) = log_level {
                zx::Status::ok(device.set_min_driver_log_severity(log_level.clone().into()).await?)
                    .map_err(|err| format_err!("{:?}", err))?;
                println!("Set {} log level to {}", device_path, log_level);
            } else {
                let (status, severity) = device.get_min_driver_log_severity().await?;
                zx::Status::ok(status).map_err(|err| format_err!("{:?}", err))?;
                println!("Current log severity: {}", LogLevel::try_from(severity)?);
            }
        }
    }
    Ok(())
}

fn connect_to_device(dev: DirectoryProxy, device_path: &str) -> Result<ControllerProxy> {
    let (client, server) = fidl::endpoints::create_proxy::<fidl_fuchsia_io::NodeMarker>()?;

    dev.open(
        fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        0,
        device_path,
        server,
    )?;

    Ok(ControllerProxy::new(client.into_channel().unwrap()))
}
