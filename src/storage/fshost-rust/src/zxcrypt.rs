// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::device::Device,
    anyhow::{bail, Context, Error},
    device_watcher::recursive_wait_and_open_node,
    fidl::endpoints::Proxy,
    fidl_fuchsia_hardware_block_encrypted::DeviceManagerProxy,
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
};

/// Set up zxcrypt on the given device. It attempts to unseal first, and if the unseal fails
/// because we had the wrong keys, it formats the device and unseals that. This function assumes
/// the zxcrypt driver is already bound, but it does do the proper waiting for the device to
/// appear.
pub async fn unseal_or_format(device: &mut dyn Device) -> Result<(), Error> {
    let controller = fuchsia_fs::directory::open_in_namespace(
        device.topological_path(),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )?;
    let zxcrypt = recursive_wait_and_open_node(&controller, "zxcrypt")
        .await
        .context("waiting for zxcrypt device")?;
    let zxcrypt = DeviceManagerProxy::new(zxcrypt.into_channel().unwrap());

    // TODO(fxbug.dev/109067): use hardware keys
    let key = [0u8; 32];

    // Try to unseal.
    let status = match zx::ok(zxcrypt.unseal(&key, 0).await?) {
        Ok(()) => return Ok(()),
        Err(status) => status,
    };

    // We only format the device if the error message indicates we failed to unseal because we had
    // the wrong keys.
    if status != zx::Status::ACCESS_DENIED {
        bail!("failed to unseal zxcrypt: {}", status);
    }

    // Format the device then unseal that.
    zx::ok(zxcrypt.format(&key, 0).await?).context("format returned error")?;
    zx::ok(zxcrypt.unseal(&key, 0).await?)
        .context("unseal of newly formatted device returned error")?;
    Ok(())
}
