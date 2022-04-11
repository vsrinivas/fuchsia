// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    super::common, anyhow::Result, args::ListDevicesCommand,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol, fidl_fuchsia_driver_development as fdd,
};

pub async fn list_devices(
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: ListDevicesCommand,
) -> Result<()> {
    let service = common::get_development_proxy(remote_control, cmd.select).await?;
    let device_info = match cmd.device {
        Some(device) => common::get_device_info(&service, &[device]).await?,
        None => common::get_device_info(&service, &[]).await?,
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
            println!("{0: <9}: {1:?}", "Flags", device.flags.unwrap_or(fdd::DeviceFlags::empty()));
            if let Some(property_list) = device.property_list {
                let count = property_list.props.len();
                println!("{} Properties", count);
                let mut idx = 0;
                for prop in property_list.props {
                    let id_name = bind::compiler::get_deprecated_key_identifiers()
                        .get(&(prop.id as u32))
                        .map(std::clone::Clone::clone)
                        .unwrap_or_else(|| format!("{:#08}", prop.id));
                    println!(
                        "[{0: >2}/ {1: >2}] : Value {2:#08x} Id {3}",
                        idx, count, prop.value, id_name
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
