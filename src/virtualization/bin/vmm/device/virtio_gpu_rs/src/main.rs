// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod gpu_device;
mod resource;
mod scanout;
mod wire;

use {
    crate::gpu_device::GpuDevice,
    crate::scanout::Scanout,
    anyhow::{anyhow, Context},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_virtualization_hardware::VirtioGpuRequestStream,
    fuchsia_component::server,
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    tracing,
};

async fn run_virtio_gpu(mut virtio_gpu_fidl: VirtioGpuRequestStream) -> Result<(), anyhow::Error> {
    // Receive start info as first message.
    let (start_info, _keyboard_listener, _pointer_listener, responder) = virtio_gpu_fidl
        .try_next()
        .await?
        .ok_or(anyhow!("Failed to read fidl message from the channel."))?
        .into_start()
        .ok_or(anyhow!("Start should be the first message sent."))?;

    // Prepare the device builder
    let (device_builder, guest_mem) = machina_virtio_device::from_start_info(start_info)?;

    responder.send()?;

    // Complete the setup of queues and get a device.
    let mut virtio_device_fidl = virtio_gpu_fidl.cast_stream();
    let (device, ready_responder) = machina_virtio_device::config_builder_from_stream(
        device_builder,
        &mut virtio_device_fidl,
        &[wire::CONTROLQ, wire::CURSORQ][..],
        &guest_mem,
    )
    .await
    .context("Failed to initialize device.")?;

    // Create the device and attempt to attach an initial scanout. This may fail, ex if run within
    // an environment without Flatland or a graphical shell.
    let mut gpu_device = match Scanout::create().await {
        Ok(scanout) => GpuDevice::with_scanout(&guest_mem, scanout),
        Err(e) => {
            tracing::warn!("Failed to create scanout: {}", e);
            GpuDevice::new(&guest_mem)
        }
    };

    let control_stream = device.take_stream(wire::CONTROLQ)?;
    let cursor_stream = device.take_stream(wire::CURSORQ)?;
    ready_responder.send()?;

    futures::try_join!(
        device
            .run_device_notify(virtio_device_fidl)
            .map_err(|e| anyhow!("run_device_notify: {}", e)),
        gpu_device.process_queues(control_stream, cursor_stream),
    )?;
    Ok(())
}

#[fuchsia::main(logging = true, threads = 1)]
async fn main() -> Result<(), anyhow::Error> {
    let mut fs = server::ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream: VirtioGpuRequestStream| stream);
    fs.take_and_serve_directory_handle().context("Error starting server")?;
    fs.for_each_concurrent(None, |stream| async {
        if let Err(e) = run_virtio_gpu(stream).await {
            tracing::error!("Error running virtio_gpu service: {}", e);
        }
    })
    .await;
    Ok(())
}
