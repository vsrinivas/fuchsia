// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_virtualization_hardware::VirtioBlockRequestStream,
    fuchsia_component::server,
    fuchsia_syslog::{self as syslog},
    futures::{StreamExt, TryStreamExt},
};

const VIRTIO_BLOCK_REQUEST_QUEUE: u16 = 0;

async fn run_virtio_block(
    mut virtio_block_fidl: VirtioBlockRequestStream,
) -> Result<(), anyhow::Error> {
    // Receive start info as first message.
    let (start_info, _id, _mode, _format, _client, responder) = virtio_block_fidl
        .try_next()
        .await?
        .ok_or(anyhow!("Failed to read fidl message from the channel."))?
        .into_start()
        .ok_or(anyhow!("Start should be the first message sent."))?;

    // Prepare the device builder
    let (device_builder, guest_mem) = machina_virtio_device::from_start_info(start_info)?;
    // TODO(fxbug.dev/95529: Report accurate values here once implemented.
    responder.send(0 /* capacity */, 0 /* block size */)?;

    // Complete the setup of queues and get a device.
    let mut virtio_device_fidl = virtio_block_fidl.cast_stream();
    let (device, ready_responder) = machina_virtio_device::config_builder_from_stream(
        device_builder,
        &mut virtio_device_fidl,
        &[VIRTIO_BLOCK_REQUEST_QUEUE][..],
        &guest_mem,
    )
    .await
    .context("Failed to initialize device.")?;

    // Initialize all queues.
    let _request_stream = device.take_stream(VIRTIO_BLOCK_REQUEST_QUEUE)?;
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
