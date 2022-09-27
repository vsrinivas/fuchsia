// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    anyhow::{Context, Result},
    args::ReadCommand,
    fidl_fuchsia_io as fio,
    std::io::Write,
};

pub async fn read(
    cmd: &ReadCommand,
    writer: &mut impl Write,
    dev: &fio::DirectoryProxy,
) -> Result<()> {
    let device = super::connect_to_i2c_device(&cmd.device_path, dev)
        .context("Failed to connect to I2C device")?;
    let byte = super::read_byte_from_i2c_device(&device, &cmd.address)
        .await
        .context("Failed to read byte from I2C device")?;
    write!(writer, "Read from")?;
    for byte in cmd.address.iter() {
        write!(writer, " {:#04x}", byte)?;
    }
    writeln!(writer, ": {:#04x}", byte)?;
    Ok(())
}
