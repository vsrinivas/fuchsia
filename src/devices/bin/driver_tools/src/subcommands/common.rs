// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    component_hub::{io::Directory, select::find_components},
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol, fidl_fuchsia_driver_development as fdd,
    fidl_fuchsia_driver_playground, fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
    selectors::{self, VerboseError},
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

pub async fn remotecontrol_connect<S: ProtocolMarker>(
    remote_control: &fremotecontrol::RemoteControlProxy,
    selector: &str,
) -> Result<S::Proxy, anyhow::Error> {
    let (proxy, server_end) = fidl::endpoints::create_proxy::<S>()
        .with_context(|| format!("failed to create proxy to {}", S::NAME))?;
    let _: fremotecontrol::ServiceMatch = remote_control
        .connect(selectors::parse_selector::<VerboseError>(selector)?, server_end.into_channel())
        .await?
        .map_err(|e| {
            anyhow::anyhow!("failed to connect to {} as {}: {:?}", S::NAME, selector, e)
        })?;
    Ok(proxy)
}

async fn find_components_with_capability(
    remote_proxy: &fremotecontrol::RemoteControlProxy,
    capability: &str,
) -> Result<Vec<String>> {
    let (root, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .context("creating hub root proxy")?;
    remote_proxy
        .open_hub(dir_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening hub")?;
    let hub_dir = Directory::from_proxy(root);
    Ok(find_components(capability.to_string(), hub_dir)
        .await?
        .exposed
        .iter()
        .map(|c| c.to_string().split_off(1))
        .collect())
}

/// Find the components that expose a given capability, and let the user
/// request which component they would like to connect to.
pub async fn user_choose_selector(
    remote_control: &fremotecontrol::RemoteControlProxy,
    capability: &str,
) -> Result<String> {
    let capabilities = find_components_with_capability(&remote_control, capability).await?;
    println!("Please choose which component to connect to:");
    for (i, component) in capabilities.iter().enumerate() {
        println!("    {}: {}", i, component)
    }

    let mut line_editor = rustyline::Editor::<()>::new();
    loop {
        let line = line_editor.readline("$ ")?;
        let choice = line.trim().parse::<usize>();
        if choice.is_err() {
            println!("Error: please choose a value.");
            continue;
        }
        let choice = choice.unwrap();
        if choice >= capabilities.len() {
            println!("Error: please choose a correct value.");
            continue;
        }
        // TODO(fxbug.dev/85516): We have to replace ':' with '*' because they are parsed
        // incorrectly in a selector.
        return Ok(capabilities[choice].replace(":", "*") + ":expose:" + capability);
    }
}

pub async fn get_devfs_proxy(
    remote_control: fremotecontrol::RemoteControlProxy,
    select: bool,
) -> Result<fio::DirectoryProxy> {
    let selector = match select {
        true => user_choose_selector(&remote_control, "dev").await?,
        false => "bootstrap/driver_manager:expose:dev".to_string(),
    };
    remotecontrol_connect::<fio::DirectoryMarker>(&remote_control, &selector).await
}

pub async fn get_development_proxy(
    remote_control: fremotecontrol::RemoteControlProxy,
    select: bool,
) -> Result<fdd::DriverDevelopmentProxy> {
    let selector = match select {
        true => {
            user_choose_selector(&remote_control, "fuchsia.driver.development.DriverDevelopment")
                .await?
        }
        false => "bootstrap/driver_manager:expose:fuchsia.driver.development.DriverDevelopment"
            .to_string(),
    };
    remotecontrol_connect::<fdd::DriverDevelopmentMarker>(&remote_control, &selector).await
}

pub async fn get_registrar_proxy(
    remote_control: fremotecontrol::RemoteControlProxy,
    select: bool,
) -> Result<fidl_fuchsia_driver_registrar::DriverRegistrarProxy> {
    let selector = match select {
        true => {
            user_choose_selector(&remote_control, "fuchsia.driver.registrar.DriverRegistrar")
                .await?
        }
        false => {
            "bootstrap/driver_index:expose:fuchsia.driver.registrar.DriverRegistrar".to_string()
        }
    };
    remotecontrol_connect::<fidl_fuchsia_driver_registrar::DriverRegistrarMarker>(
        &remote_control,
        &selector,
    )
    .await
}

pub async fn get_playground_proxy(
    remote_control: fremotecontrol::RemoteControlProxy,
    select: bool,
) -> Result<fidl_fuchsia_driver_playground::ToolRunnerProxy> {
    let selector = if select {
        user_choose_selector(&remote_control, "fuchsia.driver.playground.ToolRunner").await?
    } else {
        "core/driver_playground:expose:fuchsia.driver.playground.ToolRunner".to_string()
    };

    remotecontrol_connect::<fidl_fuchsia_driver_playground::ToolRunnerMarker>(
        &remote_control,
        &selector,
    )
    .await
}
