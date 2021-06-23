// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    ffx_core::ffx_plugin,
    ffx_driver_register_args::DriverRegisterCommand,
    fidl_fuchsia_driver_registrar::DriverRegistrarProxy,
    fidl_fuchsia_pkg::PackageUrl,
};

#[ffx_plugin(
    "driver_enabled",
    DriverRegistrarProxy = "core/appmgr:out:fuchsia.driver.registrar.DriverRegistrar"
)]
pub async fn register(
    driver_registrar_proxy: DriverRegistrarProxy,
    cmd: DriverRegisterCommand,
) -> Result<()> {
    println!("Notifying the driver manager that there might be a new version of {}", cmd.url);
    driver_registrar_proxy
        .register(&mut PackageUrl { url: cmd.url.to_string() })
        .await?
        .map_err(|err| format_err!("{:?}", err))
}
