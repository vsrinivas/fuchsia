// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    ffx_core::ffx_plugin,
    ffx_driver::get_device_info,
    ffx_driver_dump_args::DriverDumpCommand,
    fidl_fuchsia_driver_development as fdd,
    std::collections::{BTreeMap, VecDeque},
};

fn extract_name<'a>(topo_path: &'a str) -> &'a str {
    let (_, name) = topo_path.rsplit_once('/').unwrap_or(("", &topo_path));
    name
}

#[ffx_plugin(
    "driver_enabled",
    fdd::DriverDevelopmentProxy = "bootstrap/driver_manager:expose:fuchsia.driver.development.DriverDevelopment"
)]
pub async fn dump(service: fdd::DriverDevelopmentProxy, cmd: DriverDumpCommand) -> Result<()> {
    let device_info = get_device_info(&service, &mut [].iter().map(String::as_str)).await?;

    let device_map = device_info
        .iter()
        .map(|device| {
            if let Some(id) = device.id {
                Ok((id, device))
            } else {
                Err(format_err!("Missing device id"))
            }
        })
        .collect::<Result<BTreeMap<_, _>>>()?;

    let platform_device = device_info
        .iter()
        .find(|device| {
            if let Some(topo_path) = &device.topological_path {
                topo_path.as_str() == "/dev/sys/platform"
            } else {
                false
            }
        })
        .ok_or(format_err!("Missing platform device"))?;

    let mut stack = VecDeque::new();
    stack.push_front((platform_device, 0));
    if cmd.graph {
        println!("digraph {{");
        for device in &device_info {
            if let Some(child_ids) = &device.child_ids {
                for id in child_ids.iter().rev() {
                    let child = &device_map[&id];
                    println!(
                        "     \"{}\" -> \"{}\"",
                        extract_name(
                            &device
                                .topological_path
                                .as_ref()
                                .ok_or(format_err!("Missing topological path"))?
                        ),
                        extract_name(
                            &child
                                .topological_path
                                .as_ref()
                                .ok_or(format_err!("Missing topological path"))?
                        )
                    );
                }
            }
        }
        println!("}}");
    } else {
        while let Some((device, tabs)) = stack.pop_front() {
            println!(
                "{:indent$}[{}] pid={} {}",
                "",
                extract_name(
                    &device
                        .topological_path
                        .as_ref()
                        .ok_or(format_err!("Missing topological path"))?
                ),
                device.driver_host_koid.as_ref().ok_or(format_err!("Missing driver host koid"))?,
                device
                    .bound_driver_libname
                    .as_ref()
                    .ok_or(format_err!("Missing driver libname"))?,
                indent = tabs * 3,
            );
            if let Some(child_ids) = &device.child_ids {
                for id in child_ids.iter().rev() {
                    stack.push_front((device_map[&id], tabs + 1));
                }
            }
        }
    }
    Ok(())
}
