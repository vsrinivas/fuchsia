// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    anyhow::{Context, Result},
    args::PingCommand,
    fidl_fuchsia_io as fio,
    std::{io::Write, path::Path},
};

pub async fn ping(
    _cmd: &PingCommand,
    writer: &mut impl Write,
    dev: &fio::DirectoryProxy,
) -> Result<()> {
    async fn ping_device(device_name: &str, dir: &fio::DirectoryProxy) -> Result<()> {
        let device = super::connect_to_i2c_device(&Path::new(device_name), &dir)
            .context("Failed to connect to I2C device")?;
        super::read_byte_from_i2c_device(&device, &[0]).await.context("Failed to read byte")?;
        Ok(())
    }

    const I2C_DEV_PATH: &str = "class/i2c";
    let dir = fuchsia_fs::open_directory(
        dev,
        &Path::new(I2C_DEV_PATH),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )
    .with_context(|| format!("Failed to open \"{}\"", I2C_DEV_PATH))?;
    let dirents = fuchsia_fs::directory::readdir(&dir).await.context("Failed to read directory")?;
    for dirent in dirents.iter() {
        writeln!(
            writer,
            "{}/{}: {}",
            I2C_DEV_PATH,
            &dirent.name,
            match ping_device(&dirent.name, &dir).await {
                Ok(_) => "OK".to_owned(),
                Err(err) => format!("ERROR: {:?}", err),
            }
        )?;
    }
    Ok(())
}
