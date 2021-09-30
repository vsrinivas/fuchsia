// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_developer_remotecontrol as fremotecontrol;
use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_driver::*,
    ffx_driver_list_devices_args::DriverListDevicesCommand,
    fidl_fuchsia_driver_development::{DeviceFlags, DriverDevelopmentMarker},
};

#[ffx_plugin()]
pub async fn list_devices(
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: DriverListDevicesCommand,
) -> Result<()> {
    let selector = match cmd.select {
        true => {
            user_choose_selector(&remote_control, "fuchsia.driver.development.DriverDevelopment")
                .await?
        }
        false => "bootstrap/driver_manager:expose:fuchsia.driver.development.DriverDevelopment"
            .to_string(),
    };

    let service =
        remotecontrol_connect::<DriverDevelopmentMarker>(&remote_control, &selector).await?;
    let device_info = match cmd.device {
        Some(device) => get_device_info(&service, &mut [device].iter().map(String::as_str)).await?,
        None => get_device_info(&service, &mut [].iter().map(String::as_str)).await?,
    };

    if cmd.verbose {
        for device in device_info {
            let topo_path = device
                .topological_path
                .map(|s| s.strip_prefix("/dev/").unwrap().to_string())
                .unwrap_or("".to_string());
            let (_, name) = topo_path.rsplit_once('/').unwrap_or(("", &topo_path));
            println!("{0: <9}: {1}", "Name", name);
            println!("{0: <9}: {1}", "Topo Path", topo_path);
            println!(
                "{0: <9}: {1}",
                "Driver",
                device.bound_driver_libname.unwrap_or("".to_string()),
            );
            println!("{0: <9}: {1:?}", "Flags", device.flags.unwrap_or(DeviceFlags::empty()));
            if let Some(property_list) = device.property_list {
                let count = property_list.props.len();
                println!("{} Properties", count);
                let mut idx = 0;
                for prop in property_list.props {
                    println!(
                        "[{0: >2}/ {1: >2}] : Value {2:#08x} Id {3:#08x}",
                        idx, count, prop.value, prop.id
                    );
                    idx += 1;
                }
                let count = property_list.str_props.len();
                println!("{} String Properties", count);
                idx = 0;
                for prop in property_list.str_props {
                    println!(
                        "[{0: >2}/ {1: >2}] : Key {2} Value {3:?}",
                        idx, count, prop.key, prop.value
                    );
                    idx += 1;
                }
            } else {
                println!("0 Properties");
                println!("0 String Properties");
            }
            println!("");
        }
    } else {
        for device in device_info {
            if let Some(path) = device.topological_path {
                println!("{}", path);
            }
        }
    }
    Ok(())
}
