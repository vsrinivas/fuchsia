// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    anyhow::{Context, Result},
    args::WriteCommand,
    fidl_fuchsia_hardware_i2c as fi2c, fidl_fuchsia_io as fio, fuchsia_zircon_status as zx,
    std::io::Write,
};

pub async fn write(
    cmd: &WriteCommand,
    writer: &mut impl Write,
    dev: &fio::DirectoryProxy,
) -> Result<()> {
    let device = super::connect_to_i2c_device(&cmd.device_path, dev)
        .context("Failed to connect to I2C device")?;
    let transactions = vec![fi2c::Transaction {
        data_transfer: Some(fi2c::DataTransfer::WriteData(cmd.data.clone())),
        ..fi2c::Transaction::EMPTY
    }];
    device
        .transfer(&mut transactions.into_iter())
        .await
        .context("Failed to send request to transfer write transaction to I2C device")?
        .map_err(|status| zx::Status::from_raw(status))
        .context("Failed to transfer write transaction to I2C device")?;
    write!(writer, "Write:")?;
    for byte in cmd.data.iter() {
        write!(writer, " {:#04x}", byte)?;
    }
    writeln!(writer, "")?;
    Ok(())
}
