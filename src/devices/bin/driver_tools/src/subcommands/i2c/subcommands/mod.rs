// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod ping;
pub mod read;
pub mod transact;
pub mod write;

use {
    anyhow::{Context, Result},
    fidl::endpoints::Proxy,
    fidl_fuchsia_hardware_i2c as fi2c, fidl_fuchsia_io as fio, fuchsia_zircon_status as zx,
    std::{convert::AsRef, fmt::Debug, path::Path},
};

fn connect_to_i2c_device(
    device_path: impl AsRef<Path> + Debug,
    root: &fio::DirectoryProxy,
) -> Result<fi2c::DeviceProxy> {
    let (proxy, server) = fidl::endpoints::create_proxy::<fio::NodeMarker>()?;
    let device_path = device_path
        .as_ref()
        .as_os_str()
        .to_str()
        .ok_or(anyhow::anyhow!("Failed to get device path string"))?;
    root.open(
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        0,
        device_path,
        server,
    )
    .context("Failed to open I2C device file")?;
    Ok(fi2c::DeviceProxy::new(proxy.into_channel().unwrap()))
}

async fn read_byte_from_i2c_device(device: &fi2c::DeviceProxy, address: &[u8]) -> Result<u8> {
    let transactions = vec![
        fi2c::Transaction {
            data_transfer: Some(fi2c::DataTransfer::WriteData(address.to_owned())),
            ..fi2c::Transaction::EMPTY
        },
        fi2c::Transaction {
            data_transfer: Some(fi2c::DataTransfer::ReadSize(1)),
            stop: Some(true),
            ..fi2c::Transaction::EMPTY
        },
    ];
    let data = device
        .transfer(&mut transactions.into_iter())
        .await
        .context("Failed to send request to transfer transactions to I2C device")?
        .map_err(|status| zx::Status::from_raw(status))
        .context("Failed to transfer transactions to I2C device")?;
    if data.len() != 1 && data[0].len() != 1 {
        anyhow::bail!("Data size returned by I2C device is incorrect");
    }
    Ok(data[0][0])
}
