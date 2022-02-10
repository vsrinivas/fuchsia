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

trait DeviceInfoPrinter {
    fn print(&self, tabs: usize) -> Result<()>;

    fn print_graph(&self, child: &fdd::DeviceInfo) -> Result<()>;
}

struct DFv1Device(fdd::DeviceInfo);

impl DFv1Device {
    fn extract_name<'b>(topological_path: &'b str) -> &'b str {
        let (_, name) = topological_path.rsplit_once('/').unwrap_or(("", &topological_path));
        name
    }
}

impl DeviceInfoPrinter for DFv1Device {
    fn print(&self, tabs: usize) -> Result<()> {
        println!(
            "{:indent$}[{}] pid={} {}",
            "",
            Self::extract_name(
                self.0.topological_path.as_ref().ok_or(format_err!("Missing topological path"))?
            ),
            self.0.driver_host_koid.as_ref().ok_or(format_err!("Missing driver host KOID"))?,
            self.0.bound_driver_libname.as_ref().ok_or(format_err!("Missing driver libname"))?,
            indent = tabs * 3,
        );
        Ok(())
    }

    fn print_graph(&self, child: &fdd::DeviceInfo) -> Result<()> {
        println!(
            "     \"{}\" -> \"{}\"",
            Self::extract_name(
                &self
                    .0
                    .topological_path
                    .as_ref()
                    .ok_or(format_err!("Device missing topological path"))?
            ),
            Self::extract_name(
                &child
                    .topological_path
                    .as_ref()
                    .ok_or(format_err!("Child device missing topological path"))?
            )
        );
        Ok(())
    }
}

struct DFv2Node(fdd::DeviceInfo);

impl DFv2Node {
    fn extract_name<'b>(moniker: &'b str) -> &'b str {
        let (_, name) = moniker.rsplit_once('.').unwrap_or(("", &moniker));
        name
    }
}

impl DeviceInfoPrinter for DFv2Node {
    fn print(&self, tabs: usize) -> Result<()> {
        // TODO(fxbug.dev/90735): Print driver host's KOID
        println!(
            "{:indent$}[{}] {}",
            "",
            Self::extract_name(self.0.moniker.as_ref().ok_or(format_err!("Missing moniker"))?),
            self.0.bound_driver_url.as_deref().unwrap_or(""),
            indent = tabs * 3,
        );
        Ok(())
    }

    fn print_graph(&self, child: &fdd::DeviceInfo) -> Result<()> {
        println!(
            "     \"{}\" -> \"{}\"",
            Self::extract_name(
                &self.0.moniker.as_ref().ok_or(format_err!("Node missing moniker"))?
            ),
            // If the device is a DFv2 node then it is assumed that the child is a DFv2 node.
            Self::extract_name(
                &child.moniker.as_ref().ok_or(format_err!("Child node missing moniker"))?
            )
        );
        Ok(())
    }
}

enum Device {
    V1(DFv1Device),
    V2(DFv2Node),
}

impl Device {
    pub fn get_device_info(&self) -> &fdd::DeviceInfo {
        match self {
            Device::V1(device) => &device.0,
            Device::V2(node) => &node.0,
        }
    }
}

impl DeviceInfoPrinter for Device {
    fn print(&self, tabs: usize) -> Result<()> {
        match self {
            Device::V1(device) => device.print(tabs),
            Device::V2(device) => device.print(tabs),
        }
    }

    fn print_graph(&self, child: &fdd::DeviceInfo) -> Result<()> {
        match self {
            Device::V1(device) => device.print_graph(child),
            Device::V2(node) => node.print_graph(child),
        }
    }
}

impl std::convert::From<fdd::DeviceInfo> for Device {
    fn from(device_info: fdd::DeviceInfo) -> Device {
        fn is_dfv2_node(device_info: &fdd::DeviceInfo) -> bool {
            device_info.bound_driver_libname.is_none()
        }

        if is_dfv2_node(&device_info) {
            Device::V2(DFv2Node(device_info))
        } else {
            Device::V1(DFv1Device(device_info))
        }
    }
}

fn print_tree(root: &Device, device_map: &BTreeMap<u64, &Device>) -> Result<()> {
    let mut stack = VecDeque::new();
    stack.push_front((root, 0));
    while let Some((device, tabs)) = stack.pop_front() {
        device.print(tabs)?;
        if let Some(child_ids) = &device.get_device_info().child_ids {
            for id in child_ids.iter().rev() {
                if let Some(child) = device_map.get(id) {
                    stack.push_front((child, tabs + 1));
                }
            }
        }
    }
    Ok(())
}

#[ffx_plugin()]
pub async fn dump(
    remote_control: fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
    cmd: DriverDumpCommand,
) -> Result<()> {
    let service = ffx_driver::get_development_proxy(remote_control, cmd.select).await?;
    let devices: Vec<Device> =
        get_device_info(&service, &[]).await?.into_iter().map(|device| device.into()).collect();

    let device_map = devices
        .iter()
        .map(|device| {
            let device_info = device.get_device_info();
            if let Some(id) = device_info.id {
                Ok((id, device))
            } else {
                Err(format_err!("Missing device id"))
            }
        })
        .collect::<Result<BTreeMap<_, _>>>()?;

    if cmd.graph {
        println!("digraph {{");
        println!("     forcelabels = true; splines=\"ortho\"; ranksep = 1.2; nodesep = 0.5;");
        println!("     node [ shape = \"box\" color = \"#2a5b4f\" penwidth = 2.25 fontname = \"prompt medium\" fontsize = 10 margin = 0.22 ];");
        println!("     edge [ color = \"#37474f\" penwidth = 1 style = dashed fontname = \"roboto mono\" fontsize = 10 ];");
        for device in devices.iter() {
            if let Some(child_ids) = &device.get_device_info().child_ids {
                for id in child_ids.iter().rev() {
                    let child = &device_map[&id];
                    device.print_graph(child.get_device_info())?;
                }
            }
        }
        println!("}}");
    } else {
        let roots = devices.iter().filter(|device| {
            let device_info = device.get_device_info();
            if let Some(parent_ids) = device_info.parent_ids.as_ref() {
                for parent_id in parent_ids.iter() {
                    if device_map.contains_key(parent_id) {
                        return false;
                    }
                }
                true
            } else {
                true
            }
        });

        for root in roots {
            print_tree(root, &device_map)?;
        }
    }
    Ok(())
}
