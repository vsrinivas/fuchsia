// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod backend;
#[cfg(test)]
mod backend_test;
mod block_device;
mod copy_on_write_backend;
mod file_backend;
mod memory_backend;
mod qcow_backend;
mod remote_backend;
mod wire;

use {
    crate::backend::*,
    crate::block_device::*,
    crate::copy_on_write_backend::CopyOnWriteBackend,
    crate::file_backend::FileBackend,
    crate::memory_backend::MemoryBackend,
    crate::qcow_backend::QcowBackend,
    crate::remote_backend::RemoteBackend,
    anyhow::{anyhow, Context},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_virtualization::{BlockFormat, BlockMode},
    fidl_fuchsia_virtualization_hardware::VirtioBlockRequestStream,
    fuchsia_component::server,
    fuchsia_zircon as zx,
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    virtio_device::chain::ReadableChain,
};

async fn create_backend(
    format: BlockFormat,
    mode: BlockMode,
    channel: zx::Channel,
) -> Result<Box<dyn BlockBackend>, anyhow::Error> {
    let backend: Box<dyn BlockBackend> = match format {
        BlockFormat::File => Box::new(FileBackend::new(channel)?),
        BlockFormat::Block => Box::new(RemoteBackend::new(channel).await?),
        BlockFormat::Qcow => {
            if let BlockMode::ReadWrite = mode {
                return Err(anyhow!("Writes to QCOW files is not supported"));
            } else {
                Box::new(QcowBackend::new(channel)?)
            }
        }
    };
    if mode == BlockMode::VolatileWrite {
        let size = backend.get_attrs().await?.capacity.to_bytes().unwrap();
        let (memory_backend, _) = MemoryBackend::with_size(size as usize);
        Ok(Box::new(CopyOnWriteBackend::new(backend, Box::new(memory_backend)).await?))
    } else {
        Ok(backend)
    }
}

async fn run_virtio_block(
    mut virtio_block_fidl: VirtioBlockRequestStream,
) -> Result<(), anyhow::Error> {
    // Receive start info as first message.
    let (start_info, id, mode, format, client, responder) = virtio_block_fidl
        .try_next()
        .await?
        .ok_or(anyhow!("Failed to read fidl message from the channel."))?
        .into_start()
        .ok_or(anyhow!("Start should be the first message sent."))?;

    // Prepare the device builder
    let (device_builder, guest_mem) = machina_virtio_device::from_start_info(start_info)?;

    let backend = create_backend(format, mode, client).await?;
    let block_device = BlockDevice::new(id, mode.into(), backend).await?;
    responder.send(
        block_device.attrs().capacity.to_bytes().unwrap(),
        block_device.attrs().block_size.unwrap_or(wire::VIRTIO_BLOCK_SECTOR_SIZE as u32),
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
    let request_stream = device.take_stream(wire::VIRTIO_BLOCK_REQUEST_QUEUE)?;
    ready_responder.send()?;

    futures::try_join!(
        device
            .run_device_notify(virtio_device_fidl)
            .map_err(|e| anyhow!("run_device_notify: {}", e)),
        request_stream.map(|chain| Ok(chain)).try_for_each_concurrent(None, {
            let guest_mem = &guest_mem;
            let block_device = &block_device;
            move |chain| async move {
                block_device.process_chain(ReadableChain::new(chain, guest_mem)).await
            }
        })
    )?;
    Ok(())
}

#[fuchsia::main(logging = true, threads = 1)]
async fn main() -> Result<(), anyhow::Error> {
    let mut fs = server::ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream: VirtioBlockRequestStream| stream);
    fs.take_and_serve_directory_handle().context("Error starting server")?;
    fs.for_each_concurrent(None, |stream| async {
        if let Err(err) = run_virtio_block(stream).await {
            tracing::error!(%err, "Error running virtio_block service");
        }
    })
    .await;
    Ok(())
}
