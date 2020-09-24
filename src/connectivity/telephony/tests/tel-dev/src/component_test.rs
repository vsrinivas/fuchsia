// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::isolated_devmgr,
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_device::ControllerSynchronousProxy,
    fuchsia_async::{DurationExt, Timer},
    fuchsia_zircon::{self as zx, AsHandleRef, MessageBuf},
    std::fs::File,
    std::path::Path,
};

/// Resize vec to the size of qmi message
// TODO (jiamingw): get message from driver without padding (fxbug.dev/39128)
pub fn qmi_vec_resize(qmi_vec: &mut Vec<u8>) -> Result<(), Error> {
    if qmi_vec.len() <= 1 {
        return Err(format_err!("qmi_vec_resize: QMI msg too short"));
    }
    let qmi_len = (qmi_vec[1] + 1) as usize;
    if qmi_vec.len() < qmi_len {
        return Err(format_err!("qmi_vec_resize: truncated QMI msg"));
    }
    qmi_vec.resize(qmi_len, 0);
    Ok(())
}

pub fn is_equal_vec(lhs: &Vec<u8>, rhs: &Vec<u8>) -> bool {
    (lhs.len() == rhs.len()) && lhs.iter().zip(rhs).all(|(l, r)| l == r)
}

/// Get the only one device is added to IsolatedDevmgr in path `dir_path_str`
pub async fn get_fake_device_in_isolated_devmgr(dir_path_str: &str) -> Result<File, Error> {
    let tel_dir = isolated_devmgr::open_dir_in_isolated_devmgr(dir_path_str)
        .context("err opening tel dir")?;
    let directory_proxy = fidl_fuchsia_io::DirectoryProxy::new(
        fuchsia_async::Channel::from_channel(fdio::clone_channel(&tel_dir)?)?,
    );
    let tel_devices = files_async::readdir(&directory_proxy).await?;
    // Should have one and only one fake device available in IsolatedDevmgr
    if tel_devices.len() != 1 {
        return Err(format_err!("incorrect device number {}, shuold be 1", tel_devices.len()));
    }
    let last_device: &files_async::DirEntry = tel_devices.last().unwrap();
    let found_device_path = Path::new(dir_path_str).join(last_device.name.clone());
    let file = isolated_devmgr::open_file_in_isolated_devmgr(found_device_path)
        .context("err opening tel device")?;
    Ok(file)
}

/// Validate 0 device is presented in path `dir_path_str` in IsolatedDevmgr component
pub async fn validate_removal_of_fake_device(dir_path_str: &str) -> Result<(), Error> {
    let tel_dir = isolated_devmgr::open_dir_in_isolated_devmgr(dir_path_str)
        .context("err opening tel dir")?;
    let directory_proxy = fidl_fuchsia_io::DirectoryProxy::new(
        fuchsia_async::Channel::from_channel(fdio::clone_channel(&tel_dir)?)?,
    );
    loop {
        let tel_devices = files_async::readdir(&directory_proxy).await?;
        if tel_devices.is_empty() {
            break;
        }
    }
    Ok::<(), Error>(())
}

/// Remove device in isolated_devmgr
pub fn unbind_fake_device(device: &File) -> Result<(), Error> {
    let channel = fdio::clone_channel(device)?;
    let mut interface = ControllerSynchronousProxy::new(channel);
    interface.schedule_unbind(zx::Time::INFINITE)?.map_err(|e| zx::Status::from_raw(e).into())
}

/// Read next message from `channel`
pub fn read_next_msg_from_channel(channel: &zx::Channel) -> Result<Vec<u8>, Error> {
    let mut received_msg = MessageBuf::new();
    let channel_signals = channel.wait_handle(
        zx::Signals::CHANNEL_READABLE | zx::Signals::CHANNEL_PEER_CLOSED,
        zx::Time::INFINITE,
    )?;
    if channel_signals.contains(zx::Signals::CHANNEL_PEER_CLOSED) {
        return Err(format_err!("read_next_msg_from_channel: peer closed"));
    } else if !channel_signals.contains(zx::Signals::CHANNEL_READABLE) {
        return Err(format_err!("read_next_msg_from_channel: channel not readable"));
    }
    channel.read(&mut received_msg)?;
    Ok(received_msg.bytes().to_vec())
}

pub async fn wait_in_nanos(time_nanos: i64) {
    Timer::new(zx::Duration::from_nanos(time_nanos).after_now()).await
}
