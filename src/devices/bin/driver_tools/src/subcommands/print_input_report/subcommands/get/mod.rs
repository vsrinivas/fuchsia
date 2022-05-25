// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    anyhow::{Context, Result},
    args::GetCommand,
    fidl_fuchsia_io as fio, fuchsia_zircon_status as zx,
    futures::lock::Mutex,
    std::{io::Write, ops::DerefMut, sync::Arc},
};

pub async fn get(
    cmd: &GetCommand,
    writer: Arc<Mutex<impl Write + Send + Sync + 'static>>,
    dev: fio::DirectoryProxy,
) -> Result<()> {
    let input_device_proxy = super::connect_to_input_device(&dev, &cmd.device_path)
        .context("Failed to get input device proxy")?;
    writeln!(&mut writer.lock().await, "Reading a report from {:?}:", &cmd.device_path)
        .context("Failed to write to writer")?;
    let input_report = input_device_proxy
        .get_input_report(cmd.device_type.get_fidl())
        .await
        .context("Failed to send request to get input report")?
        .map_err(|e| zx::Status::from_raw(e))
        .context("Failed to get input report")?;
    let mut writer = writer.lock().await;
    writeln!(&mut writer, "Report from file: {:?}", &cmd.device_path)
        .context("Failed to write to writer")?;
    super::write_input_report(writer.deref_mut(), &input_report)
        .context("Failed to write input report")?;
    Ok(())
}
