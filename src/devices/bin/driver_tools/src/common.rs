// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    fidl_fuchsia_driver_development as fdd,
};

#[derive(Debug)]
pub struct DFv1Device(pub fdd::DeviceInfo);

impl DFv1Device {
    pub fn extract_name<'b>(topological_path: &'b str) -> &'b str {
        let (_, name) = topological_path.rsplit_once('/').unwrap_or(("", &topological_path));
        name
    }
}

#[derive(Debug)]
pub struct DFv2Node(pub fdd::DeviceInfo);

impl DFv2Node {
    pub fn extract_name<'b>(moniker: &'b str) -> &'b str {
        let (_, name) = moniker.rsplit_once('.').unwrap_or(("", &moniker));
        name
    }
}

#[derive(Debug)]
pub enum Device {
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

/// Combines pagination results into a single vector.
pub async fn get_device_info(
    service: &fdd::DriverDevelopmentProxy,
    device_filter: &[String],
) -> Result<Vec<fdd::DeviceInfo>> {
    let (iterator, iterator_server) =
        fidl::endpoints::create_proxy::<fdd::DeviceInfoIteratorMarker>()?;

    service
        .get_device_info(&mut device_filter.iter().map(String::as_str), iterator_server)
        .context("FIDL call to get device info failed")?;

    let mut info_result = Vec::new();
    loop {
        let mut device_info =
            iterator.get_next().await.context("FIDL call to get device info failed")?;
        if device_info.len() == 0 {
            break;
        }
        info_result.append(&mut device_info)
    }
    Ok(info_result)
}

/// Combines pagination results into a single vector.
pub async fn get_driver_info(
    service: &fdd::DriverDevelopmentProxy,
    driver_filter: &[String],
) -> Result<Vec<fdd::DriverInfo>> {
    let (iterator, iterator_server) =
        fidl::endpoints::create_proxy::<fdd::DriverInfoIteratorMarker>()?;

    service
        .get_driver_info(&mut driver_filter.iter().map(String::as_str), iterator_server)
        .context("FIDL call to get driver info failed")?;

    let mut info_result = Vec::new();
    loop {
        let mut driver_info =
            iterator.get_next().await.context("FIDL call to get driver info failed")?;
        if driver_info.len() == 0 {
            break;
        }
        info_result.append(&mut driver_info)
    }
    Ok(info_result)
}
