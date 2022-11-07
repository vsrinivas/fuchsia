// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{format_sources, get_policy, unseal_sources, KeyConsumer},
    crate::device::Device,
    anyhow::{anyhow, bail, Context, Error},
    device_watcher::recursive_wait_and_open_node,
    fidl::endpoints::Proxy,
    fidl_fuchsia_hardware_block_encrypted::DeviceManagerProxy,
    fidl_fuchsia_io as fio,
    fs_management::format::DiskFormat,
    fuchsia_zircon as zx,
};

async fn format(zxcrypt: &DeviceManagerProxy) -> Result<(), Error> {
    let policy = get_policy().await?;
    let sources = format_sources(policy);

    let mut last_err = anyhow!("no keys?");
    for source in sources {
        let key = source.get_key(KeyConsumer::Zxcrypt).await?;
        match zx::ok(zxcrypt.format(&key, 0).await?) {
            Ok(()) => return Ok(()),
            Err(status) => last_err = status.into(),
        }
    }
    Err(last_err)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum UnsealOutcome {
    Unsealed,
    ShouldFormat,
}

async fn unseal(zxcrypt: &DeviceManagerProxy) -> Result<UnsealOutcome, Error> {
    let policy = get_policy().await?;
    let sources = unseal_sources(policy);

    let mut last_res = Err(anyhow!("no keys?"));
    for source in sources {
        let key = source.get_key(KeyConsumer::Zxcrypt).await?;
        match zx::ok(zxcrypt.unseal(&key, 0).await?) {
            Ok(()) => return Ok(UnsealOutcome::Unsealed),
            Err(zx::Status::ACCESS_DENIED) => last_res = Ok(UnsealOutcome::ShouldFormat),
            Err(status) => last_res = Err(status.into()),
        }
    }
    last_res
}

/// Set up zxcrypt on the given device. It attempts to unseal first only if the detected disk
/// format is DiskFormat::Zxcrypt. If the unseal fails because we had the wrong keys, or the
/// detected disk format is not DiskFormat::Zxcrypt, it formats the device and unseals that.
/// This function assumes the zxcrypt driver is already bound, but it does do the proper waiting
/// for the device to appear.
pub async fn unseal_or_format(device: &mut dyn Device) -> Result<(), Error> {
    let controller = fuchsia_fs::directory::open_in_namespace(
        device.topological_path(),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )?;
    let zxcrypt = recursive_wait_and_open_node(&controller, "zxcrypt")
        .await
        .context("waiting for zxcrypt device")?;
    let zxcrypt = DeviceManagerProxy::new(zxcrypt.into_channel().unwrap());

    // Try to unseal if the detected disk format is DiskFormat::Zxcrypt
    if device.content_format().await? == DiskFormat::Zxcrypt {
        if let UnsealOutcome::Unsealed = unseal(&zxcrypt).await.context("unsealing zxcrypt")? {
            return Ok(());
        }
    }

    // Unsealing didn't work. Format the device then unseal that.
    tracing::warn!("failed to unseal zxcrypt, reformatting");
    format(&zxcrypt).await.context("error formatting zxcrypt")?;
    if let UnsealOutcome::ShouldFormat =
        unseal(&zxcrypt).await.context("error unsealing fresh zxcrypt")?
    {
        // Somehow we failed to unseal with the same key we just used to format
        bail!("failed to unseal zxcrypt after formatting: access denied");
    }
    Ok(())
}
