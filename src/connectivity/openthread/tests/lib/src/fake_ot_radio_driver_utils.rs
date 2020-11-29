// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{isolated_devmgr_utils::*, ot_radio_driver_utils::ot_radio_set_channel},
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_device::ControllerSynchronousProxy,
    fidl_fuchsia_lowpan_spinel::DeviceProxy,
    fuchsia_syslog::macros::*,
    fuchsia_zircon as zx,
    std::fs::File,
    std::path::Path,
};

const OT_PROTOCOL_PATH: &str = "class/ot-radio";

/// Get the ot radio device in path `dir_path_str` in isolated_devmgr
pub async fn get_ot_device_in_isolated_devmgr(dir_path_str: &str) -> Result<File, Error> {
    let ot_radio_dir =
        open_dir_in_isolated_devmgr(dir_path_str).context("opening dir in isolated devmgr")?;
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
    let file =
        open_file_in_isolated_devmgr(found_device_path).context("err opening ot radio device")?;
    Ok(file)
}

/// Get the DeviceProxy from isolated devmgr
pub async fn get_device_proxy_from_isolated_devmgr(
    dir_path_str: &str,
) -> Result<DeviceProxy, Error> {
    let ot_device_file = get_ot_device_in_isolated_devmgr(dir_path_str).await?;
    let ot_device_client_ep = ot_radio_set_channel(&ot_device_file).await?;
    let ot_device_proxy = ot_device_client_ep.into_proxy()?;
    Ok(ot_device_proxy)
}

/// Schedule unbind of a device
pub fn unbind_device_in_isolated_devmgr(device: &File) -> Result<(), Error> {
    let channel = fdio::clone_channel(device)?;
    let mut controller_proxy = ControllerSynchronousProxy::new(channel);
    controller_proxy
        .schedule_unbind(zx::Time::INFINITE)?
        .map_err(|e| zx::Status::from_raw(e).into())
}

/// Validate 0 device is presented in path `dir_path_str` in isolated_devmgr
pub async fn validate_removal_of_device_in_isolated_devmgr(
    dir_path_str: &str,
) -> Result<(), Error> {
    let protocol_dir =
        open_dir_in_isolated_devmgr(dir_path_str).context("opening dir in isolated devmgr")?;
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

pub async fn ot_radio_deinit() {
    // Remove fake ot device
    let device = get_ot_device_in_isolated_devmgr(OT_PROTOCOL_PATH).await.expect("getting device");
    unbind_device_in_isolated_devmgr(&device).expect("schedule unbind");
    validate_removal_of_device_in_isolated_devmgr(OT_PROTOCOL_PATH)
        .await
        .expect("validate removal of device");
}
