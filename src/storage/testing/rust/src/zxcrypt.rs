// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    device_watcher::recursive_wait_and_open_node,
    fidl_fuchsia_device::{ControllerMarker, ControllerProxy},
    fidl_fuchsia_hardware_block_encrypted::DeviceManagerMarker,
    fidl_fuchsia_io as fio,
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon as zx,
    std::path::{Path, PathBuf},
};

const ZXCRYPT_DRIVER_PATH: &str = "zxcrypt.so";

/// Binds the zxcrypt driver to the device at `controller`. Does not wait for the zxcrypt driver to
/// be ready.
pub async fn bind_zxcrypt_driver(controller: &ControllerProxy) -> Result<()> {
    controller
        .bind(ZXCRYPT_DRIVER_PATH)
        .await
        .context("zxcrypt driver bind fidl failure")?
        .map_err(zx::Status::from_raw)
        .context("zxcrypt driver bind returned error")?;
    Ok(())
}

/// Waits for the zxcrypt device to appear under `block_device`. Returns a path to the zxcrypt
/// device.
pub async fn wait_for_zxcrypt_driver(block_device: &Path) -> Result<PathBuf> {
    const ZXCRYPT_DEVICE_NAME: &str = "zxcrypt";
    let device = fuchsia_fs::directory::open_in_namespace(
        block_device.to_str().unwrap(),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )
    .context("block device directory open")?;
    recursive_wait_and_open_node(&device, ZXCRYPT_DEVICE_NAME)
        .await
        .context("zxcrypt device wait")?;
    Ok(block_device.join(ZXCRYPT_DEVICE_NAME))
}

/// Sets up zxcrypt on top of `block_device` using an insecure key. Returns a path to the block
/// device exposed by zxcrypt.
pub async fn set_up_insecure_zxcrypt(block_device: &Path) -> Result<PathBuf> {
    const UNSEALED_BLOCK_PATH: &str = "unsealed/block";
    let controller =
        connect_to_protocol_at_path::<ControllerMarker>(block_device.to_str().unwrap())
            .context("block device controller connect")?;
    bind_zxcrypt_driver(&controller).await.context("zxcrypt driver bind")?;

    let zxcrypt_path =
        wait_for_zxcrypt_driver(block_device).await.context("zxcrypt driver wait")?;
    let zxcrypt =
        connect_to_protocol_at_path::<DeviceManagerMarker>(zxcrypt_path.to_str().unwrap())
            .context("zxcrypt device manager connect")?;
    zx::ok(zxcrypt.format(&[0u8; 32], 0).await.context("zxcrypt format fidl failure")?)
        .context("zxcrypt format returned error")?;
    zx::ok(zxcrypt.unseal(&[0u8; 32], 0).await.context("zxcrypt unseal fidl failure")?)
        .context("zxcrypt unseal returned error")?;

    let zxcrypt_dir = fuchsia_fs::directory::open_in_namespace(
        zxcrypt_path.to_str().unwrap(),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )
    .context("zxcrypt directory open")?;
    recursive_wait_and_open_node(&zxcrypt_dir, UNSEALED_BLOCK_PATH)
        .await
        .context("zxcrypt unsealed dir wait")?;
    Ok(zxcrypt_path.join(UNSEALED_BLOCK_PATH))
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::wait_for_ramctl, fidl_fuchsia_hardware_block::BlockMarker,
        ramdevice_client::RamdiskClient, test_util::assert_lt,
    };

    const BLOCK_SIZE: u64 = 512;
    const BLOCK_COUNT: u64 = 64 * 1024 * 1024 / BLOCK_SIZE;

    #[fuchsia::test]
    async fn set_up_insecure_zxcrypt_test() {
        wait_for_ramctl().await.unwrap();
        let ramdisk = RamdiskClient::create(BLOCK_SIZE, BLOCK_COUNT).unwrap();

        let path = set_up_insecure_zxcrypt(Path::new(ramdisk.get_path()))
            .await
            .expect("Failed to set up zxcrypt");

        let block_device =
            connect_to_protocol_at_path::<BlockMarker>(path.to_str().unwrap()).unwrap();
        let info = block_device.get_info().await.unwrap();
        zx::ok(info.0).unwrap();
        let info = info.1.unwrap();
        assert_lt!(info.block_count, BLOCK_COUNT);
    }
}
