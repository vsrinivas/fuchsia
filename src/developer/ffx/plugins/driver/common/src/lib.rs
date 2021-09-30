// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    component_hub::{io::Directory, select::find_components},
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol, fidl_fuchsia_driver_development as fdd,
    fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
};

/// Combines pagination results into a single vector.
pub async fn get_device_info(
    service: &fdd::DriverDevelopmentProxy,
    device_filter: &mut dyn ExactSizeIterator<Item = &str>,
) -> Result<Vec<fdd::DeviceInfo>> {
    let (iterator, iterator_server) =
        fidl::endpoints::create_proxy::<fdd::DeviceInfoIteratorMarker>()?;

    service
        .get_device_info(device_filter, iterator_server)
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
    driver_filter: &mut dyn ExactSizeIterator<Item = &str>,
) -> Result<Vec<fdd::DriverInfo>> {
    let (iterator, iterator_server) =
        fidl::endpoints::create_proxy::<fdd::DriverInfoIteratorMarker>()?;

    service
        .get_driver_info(driver_filter, iterator_server)
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
        .connect(selectors::parse_selector(selector)?, server_end.into_channel())
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
