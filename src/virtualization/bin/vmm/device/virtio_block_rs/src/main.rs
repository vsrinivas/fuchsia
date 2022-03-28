// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod backend;
mod file_backend;
#[cfg(test)]
mod memory_backend;
mod wire;

use {
    crate::backend::*,
    crate::file_backend::FileBackend,
    anyhow::{anyhow, Context},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_virtualization::BlockFormat,
    fidl_fuchsia_virtualization_hardware::VirtioBlockRequestStream,
    fuchsia_component::server,
    fuchsia_syslog::{self as syslog},
    fuchsia_zircon as zx,
    futures::{StreamExt, TryStreamExt},
};

fn create_backend(
    format: BlockFormat,
    channel: zx::Channel,
) -> Result<Box<dyn BlockBackend>, anyhow::Error> {
    match format {
        BlockFormat::File => {
            let file_backend = FileBackend::new(channel)?;
            Ok(Box::new(file_backend))
        }
        _ => Err(anyhow!("Unsupported BlockFormat {:?}", format)),
    }
}

async fn run_virtio_block(
    mut virtio_block_fidl: VirtioBlockRequestStream,
) -> Result<(), anyhow::Error> {
    // Receive start info as first message.
    let (start_info, _id, _mode, format, client, responder) = virtio_block_fidl
        .try_next()
        .await?
        .ok_or(anyhow!("Failed to read fidl message from the channel."))?
        .into_start()
        .ok_or(anyhow!("Start should be the first message sent."))?;

    // Prepare the device builder
    let (device_builder, guest_mem) = machina_virtio_device::from_start_info(start_info)?;

    let backend = create_backend(format, client)?;
    let device_attrs = backend.get_attrs().await?;
    responder.send(
        device_attrs.capacity.to_bytes().unwrap(),
        device_attrs.block_size.unwrap_or(wire::VIRTIO_BLOCK_SECTOR_SIZE as u32),
    )?;

    // Complete the setup of queues and get a device.
    let mut virtio_device_fidl = virtio_block_fidl.cast_stream();
    let (device, ready_responder) = machina_virtio_device::config_builder_from_stream(
        device_builder,
        &mut virtio_device_fidl,
        &[wire::VIRTIO_BLOCK_REQUEST_QUEUE][..],
        &guest_mem,
    )
    .await
    .context("Failed to initialize device.")?;

    // Initialize all queues.
    let _request_stream = device.take_stream(wire::VIRTIO_BLOCK_REQUEST_QUEUE)?;
    ready_responder.send()?;

    // TODO(fxbug.dev/95529): Read and process descriptors from the request queue.
    Ok(())
}

#[fuchsia::component(logging = true, threads = 1)]
async fn main() -> Result<(), anyhow::Error> {
    let mut fs = server::ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream: VirtioBlockRequestStream| stream);
    fs.take_and_serve_directory_handle().context("Error starting server")?;
    fs.for_each_concurrent(None, |stream| async {
        if let Err(e) = run_virtio_block(stream).await {
            syslog::fx_log_err!("Error running virtio_block service: {}", e);
        }
    })
    .await;
    Ok(())
}
