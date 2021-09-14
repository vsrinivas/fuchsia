// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Result},
    ffx_core::ffx_plugin,
    ffx_driver_list_hosts_args::DriverListHostsCommand,
    fidl_fuchsia_driver_development::DriverDevelopmentProxy,
    fuchsia_zircon_status as zx,
    std::collections::{BTreeMap, BTreeSet},
};

#[ffx_plugin(
    "driver_enabled",
    DriverDevelopmentProxy = "bootstrap/driver_manager:expose:fuchsia.driver.development.DriverDevelopment"
)]
pub async fn list_hosts(
    service: DriverDevelopmentProxy,
    _cmd: DriverListHostsCommand,
) -> Result<()> {
    let device_info = service
        .get_device_info(&mut [].iter().map(String::as_str))
        .await
        .context("FIDL call to get device info failed")?
        .map_err(|err| {
            format_err!(
                "FIDL call to get device info returned an error: {}",
                zx::Status::from_raw(err)
            )
        })?;

    let mut driver_hosts = BTreeMap::new();

    for device in device_info {
        let koid = device.driver_host_koid.ok_or(format_err!("Missing driver host koid"))?;
        if let Some(url) = device.bound_driver_url {
            driver_hosts.entry(koid).or_insert(BTreeSet::new()).insert(url);
        } else if let Some(name) = device.bound_driver_libname {
            // Unbound devices have an empty name.
            if !name.is_empty() {
                driver_hosts.entry(koid).or_insert(BTreeSet::new()).insert(name);
            }
        }
    }

    for (koid, drivers) in driver_hosts {
        // Some driver hosts have a proxy loaded but nothing else. Ignore those.
        if !drivers.is_empty() {
            println!("Driver Host: {}", koid);
            for driver in drivers {
                println!("{:>4}{}", "", driver);
            }
            println!("");
        }
    }
    Ok(())
}
