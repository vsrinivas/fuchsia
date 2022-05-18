// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    anyhow::{format_err, Result},
    args::RegisterCommand,
    fidl_fuchsia_driver_development as fdd, fidl_fuchsia_driver_registrar as fdr,
    fidl_fuchsia_pkg as fpkg,
    std::io::Write,
};

pub async fn register(
    cmd: RegisterCommand,
    writer: &mut impl Write,
    driver_registrar_proxy: fdr::DriverRegistrarProxy,
    driver_development_proxy: fdd::DriverDevelopmentProxy,
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
