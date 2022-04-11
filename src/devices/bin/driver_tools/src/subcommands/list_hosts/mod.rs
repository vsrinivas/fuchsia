// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    super::common,
    anyhow::{format_err, Result},
    args::ListHostsCommand,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol,
    std::collections::{BTreeMap, BTreeSet},
};

pub async fn list_hosts(
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: ListHostsCommand,
) -> Result<()> {
    let service = common::get_development_proxy(remote_control, cmd.select).await?;
    let device_info = common::get_device_info(&service, &[]).await?;

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
