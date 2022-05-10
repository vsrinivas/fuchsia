// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    super::common,
    anyhow::{format_err, Result},
    args::RegisterCommand,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol, fidl_fuchsia_driver_development as fdd,
    fidl_fuchsia_driver_registrar as fdr, fidl_fuchsia_pkg as fpkg,
};

pub async fn register(
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: RegisterCommand,
) -> Result<()> {
    let driver_registrar_proxy =
        common::get_registrar_proxy(remote_control.clone(), cmd.select).await?;
    let driver_development_proxy =
        common::get_development_proxy(remote_control, cmd.select).await?;
    register_impl(driver_registrar_proxy, driver_development_proxy, cmd, &mut std::io::stdout())
        .await
}

pub async fn register_impl<W: std::io::Write>(
    driver_registrar_proxy: fdr::DriverRegistrarProxy,
    driver_development_proxy: fdd::DriverDevelopmentProxy,
    cmd: RegisterCommand,
    writer: &mut W,
) -> Result<()> {
    writeln!(writer, "Registering {}", cmd.url)?;
    let register_result =
        driver_registrar_proxy.register(&mut fpkg::PackageUrl { url: cmd.url.to_string() }).await?;

    match register_result {
        Ok(_) => {}
        Err(e) => {
            return Err(format_err!("Failed to register driver: {}", e));
        }
    }

    let bind_result = driver_development_proxy.bind_all_unbound_nodes().await?;

    match bind_result {
        Ok(result) => {
            if result.is_empty() {
                writeln!(
                    writer,
                    "{}\n{}\n{}",
                    "No new nodes were bound to the driver being registered.",
                    "Re-binding already bound nodes is not supported currently.",
                    "Reboot your device if you are updating an already registered driver."
                )?;
            } else {
                writeln!(writer, "Successfully bound:")?;
                for info in result {
                    writeln!(
                        writer,
                        "Node '{}', Driver '{}'.",
                        info.node_name.unwrap_or("<NA>".to_string()),
                        info.driver_url.unwrap_or("<NA>".to_string()),
                    )?;
                }
            }
        }
        Err(err) => {
            return Err(format_err!("Failed to bind nodes: {:?}", err));
        }
    };
    Ok(())
}
