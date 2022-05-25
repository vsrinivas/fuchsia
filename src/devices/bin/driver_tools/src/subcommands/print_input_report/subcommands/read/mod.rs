// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    anyhow::{Context, Result},
    args::ReadCommand,
    fidl_fuchsia_input_report as fir, fidl_fuchsia_io as fio,
    fuchsia_async::Task,
    fuchsia_zircon_status as zx,
    futures::{lock::Mutex, stream::TryStreamExt},
    std::{fmt::Debug, io::Write, ops::DerefMut, path::Path, sync::Arc},
};

async fn read_input_device(
    input_device_proxy: fir::InputDeviceProxy,
    input_device_path: impl AsRef<Path> + Debug,
    num_reads: usize,
    writer: Arc<Mutex<impl Write + Send + Sync + 'static>>,
) -> Result<()> {
    let (input_reports_reader_proxy, server) =
        fidl::endpoints::create_proxy::<fir::InputReportsReaderMarker>()?;
    input_device_proxy
        .get_input_reports_reader(server)
        .context("Failed to get input reports reader")?;
    writeln!(&mut writer.lock().await, "Reading reports from {:?}", &input_device_path)
        .context("Failed to write output")?;
    let mut reads = 0;
    while reads < num_reads {
        let input_reports = input_reports_reader_proxy
            .read_input_reports()
            .await
            .context("Failed to send request to read input reports")?
            .map_err(|e| zx::Status::from_raw(e))
            .context("Failed to read input reports")?;
        let mut writer = writer.lock().await;
        for input_report in input_reports.iter() {
            writeln!(&mut writer, "Report from file {:?}", &input_device_path)
                .context("Failed to write to writer")?;
            super::write_input_report(writer.deref_mut(), input_report)
                .context("Failed to write input report")?;
            reads += 1;
            if reads == num_reads {
                break;
            }
        }
    }
    Ok(())
}

pub async fn read(
    cmd: &ReadCommand,
    writer: Arc<Mutex<impl Write + Send + Sync + 'static>>,
    dev: fio::DirectoryProxy,
) -> Result<()> {
    if let Some(ref devpath) = cmd.devpath {
        let input_device_proxy = super::connect_to_input_device(&dev, &devpath.device_path)
            .with_context(|| {
                format!("Failed to get input device proxy from {:?}", &devpath.device_path)
            })?;
        read_input_device(input_device_proxy, &devpath.device_path, devpath.num_reads, writer)
            .await?;
        return Ok(());
    }
    let input_device_paths = super::get_all_input_device_paths(&dev)
        .await
        .context("Failed to get all input device paths")?;
    futures::pin_mut!(input_device_paths);
    while let Some(input_device_path) =
        input_device_paths.try_next().await.context("Failed to watch directory")?
    {
        let writer = Arc::clone(&writer);
        let input_device_proxy = super::connect_to_input_device(&dev, &input_device_path)
            .with_context(|| {
                format!("Failed to get input device proxy from {:?}", &input_device_path)
            })?;
        Task::spawn(async move {
            if let Err(err) =
                read_input_device(input_device_proxy, &input_device_path, usize::MAX, writer).await
            {
                log::error!("Failed to print input reports from {:?}: {}", &input_device_path, err);
            }
        })
        .detach();
    }
    Ok(())
}
