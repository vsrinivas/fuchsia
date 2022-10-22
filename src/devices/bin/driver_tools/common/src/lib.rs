// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    fidl_fuchsia_driver_development as fdd, futures,
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

/// Gets the desired DriverInfo instance.
///
/// # Arguments
/// * `driver_libname` - The driver's libname. e.g. fuchsia-pkg://domain/driver#driver/foo.so
pub async fn get_driver_by_libname(
    driver_libname: &String,
    driver_development_proxy: &fdd::DriverDevelopmentProxy,
) -> Result<fdd::DriverInfo> {
    let driver_filter: [String; 1] = [driver_libname.to_string()];
    let driver_list = get_driver_info(&driver_development_proxy, &driver_filter).await?;
    if driver_list.len() != 1 {
        return Err(anyhow!(
            "There should be exactly one match for '{}'. Found {}.",
            driver_libname,
            driver_list.len()
        ));
    }

    let mut driver_info: Option<fdd::DriverInfo> = None;

    // Confirm this is the correct match.
    let driver = &driver_list[0];
    if let Some(ref libname) = driver.libname {
        if libname == driver_libname {
            driver_info = Some(driver.clone());
        }
    }

    match driver_info {
        Some(driver) => Ok(driver),
        _ => Err(anyhow!("Did not find matching driver for: {}", driver_libname)),
    }
}

/// Gets the driver that is bound to the given device.
///
/// # Arguments
/// * `device_topo_path` - The device's topological path. e.g. sys/platform/.../device
pub async fn get_driver_by_device(
    device_topo_path: &String,
    driver_development_proxy: &fdd::DriverDevelopmentProxy,
) -> Result<fdd::DriverInfo> {
    let device_filter: [String; 1] = [device_topo_path.to_string()];
    let mut device_list = get_device_info(&driver_development_proxy, &device_filter).await?;

    if device_list.len() != 1 {
        return Err(anyhow!(
            concat!(
                "Expected 1 result for the given query but got {}. Please ",
                "adjust your query for an exact match."
            ),
            device_list.len()
        ));
    }

    let mut found_device: Option<String> = None;

    let device: Device = device_list.remove(0).into();
    match device {
        Device::V1(ref info) => match &info.0.bound_driver_libname {
            Some(bound_driver_libname) => {
                found_device = Some(bound_driver_libname.to_string());
            }
            _ => {}
        },
        Device::V2(ref _info) => {
            // TODO(fxb/112785): Querying V2 is not supported for now.
        }
    };
    match found_device {
        Some(ref driver_libname) => {
            get_driver_by_libname(&driver_libname, &driver_development_proxy).await
        }
        _ => Err(anyhow!("Did not find driver for device {}", &device_topo_path)),
    }
}

/// Gets the devices that are bound to the given driver.
///
/// # Arguments
/// * `driver_libname` - The driver's libname. e.g. fuchsia-pkg://domain/driver#driver/foo.so
pub async fn get_devices_by_driver(
    driver_libname: &String,
    driver_development_proxy: &fdd::DriverDevelopmentProxy,
) -> Result<Vec<Device>> {
    let driver_info = get_driver_by_libname(driver_libname, &driver_development_proxy);
    let empty: [String; 0] = [];
    let device_list = get_device_info(&driver_development_proxy, &empty);

    let (driver_info, device_list) = futures::join!(driver_info, device_list);
    let (driver_info, device_list) = (driver_info?, device_list?);

    let mut matches: Vec<Device> = Vec::new();
    for device_item in device_list.into_iter() {
        let device: Device = device_item.into();
        match device {
            Device::V1(ref info) => {
                if let (Some(bound_driver_libname), Some(libname)) =
                    (&info.0.bound_driver_libname, &driver_info.libname)
                {
                    if &libname == &bound_driver_libname {
                        matches.push(device);
                    }
                }
            }
            Device::V2(ref info) => {
                if let (Some(bound_driver_url), Some(url)) =
                    (&info.0.bound_driver_url, &driver_info.url)
                {
                    if &url == &bound_driver_url {
                        matches.push(device);
                    }
                }
            }
        };
    }
    Ok(matches)
}
