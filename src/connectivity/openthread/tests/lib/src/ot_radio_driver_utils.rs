// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints::{create_endpoints, ClientEnd},
    fidl_fuchsia_device::ControllerSynchronousProxy,
    fidl_fuchsia_lowpan_spinel::{DeviceMarker, DeviceSetupProxy},
    fuchsia_async as fasync,
    fuchsia_syslog::macros::*,
    fuchsia_zircon as zx,
    std::fs::File,
    std::path::Path,
};

/// Connect to driver and pass a FIDL channel which talks Device protocol
pub async fn ot_radio_set_channel(device: &File) -> Result<ClientEnd<DeviceMarker>, Error> {
    let device_setup_channel = fasync::Channel::from_channel(fdio::clone_channel(device)?)?;
    let device_setup_proxy = DeviceSetupProxy::new(device_setup_channel);
    let (client_side, server_side) = create_endpoints::<DeviceMarker>()?;
    match device_setup_proxy.set_channel(server_side.into_channel()).await {
        Ok(_r) => Ok(client_side),
        Err(e) => Err(e.into()),
    }
}

/// Get the ot radio device
pub async fn get_ot_device_in_devmgr(dir_path_str: &str) -> Result<File, Error> {
    let ot_radio_dir = File::open(dir_path_str).context("opening dir in devmgr")?;
    let directory_proxy = fidl_fuchsia_io::DirectoryProxy::new(
        fuchsia_async::Channel::from_channel(fdio::clone_channel(&ot_radio_dir)?)?,
    );
    let ot_radio_devices = files_async::readdir(&directory_proxy).await?;
    // Should have 1 device that implements OT_RADIO
    if ot_radio_devices.len() != 1 {
        return Err(format_err!("incorrect device number {}, shuold be 1", ot_radio_devices.len()));
    }
    let last_device: &files_async::DirEntry = ot_radio_devices.last().unwrap();
    let found_device_path = Path::new(dir_path_str).join(last_device.name.clone());
    fx_log_info!("device path {} got", found_device_path.to_str().unwrap());
    let file = File::open(found_device_path).context("err opening ot radio device")?;
    Ok(file)
}

/// Schedule unbind of a device
pub fn unbind_device(device: &File) -> Result<(), Error> {
    let channel = fdio::clone_channel(device)?;
    let mut controller_proxy = ControllerSynchronousProxy::new(channel);
    controller_proxy
        .schedule_unbind(zx::Time::INFINITE)?
        .map_err(|e| zx::Status::from_raw(e).into())
}

/// Validate 0 device is presented in path `dir_path_str` in devmgr
pub async fn validate_removal_of_device(dir_path_str: &str) -> Result<(), Error> {
    let protocol_dir = File::open(dir_path_str).context("opening dir in devmgr")?;
    let directory_proxy = fidl_fuchsia_io::DirectoryProxy::new(
        fuchsia_async::Channel::from_channel(fdio::clone_channel(&protocol_dir)?)?,
    );
    loop {
        let ot_devices = files_async::readdir(&directory_proxy).await?;
        if ot_devices.is_empty() {
            break;
        }
    }
    Ok::<(), Error>(())
}
