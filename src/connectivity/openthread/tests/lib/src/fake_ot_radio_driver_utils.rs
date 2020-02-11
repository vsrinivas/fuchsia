// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::isolated_devmgr_utils::*,
    anyhow::{format_err, Context as _, Error},
    fuchsia_syslog::macros::*,
    std::fs::File,
    std::path::Path,
};

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
