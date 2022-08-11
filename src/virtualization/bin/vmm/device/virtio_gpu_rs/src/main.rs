// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod gpu_command;
mod gpu_device;
mod resource;
mod scanout;
mod wire;

use {
    crate::gpu_device::GpuDevice,
    crate::scanout::FlatlandScanout,
    anyhow::{anyhow, Context},
    fidl::endpoints::{ClientEnd, RequestStream},
    fidl_fuchsia_ui_input3::KeyboardListenerMarker,
    fidl_fuchsia_virtualization_hardware::VirtioGpuRequestStream,
    fuchsia_component::server,
    futures::{select, FutureExt, StreamExt, TryFutureExt, TryStreamExt},
    tracing,
};

async fn run_virtio_gpu(mut virtio_gpu_fidl: VirtioGpuRequestStream) -> Result<(), anyhow::Error> {
    // Receive start info as first message.
    let (start_info, keyboard_listener, _pointer_listener, responder) = virtio_gpu_fidl
        .try_next()
        .await?
        .ok_or(anyhow!("Failed to read fidl message from the channel."))?
        .into_start()
        .ok_or(anyhow!("Start should be the first message sent."))?;

    // Prepare the device builder
    let (device_builder, guest_mem) = machina_virtio_device::from_start_info(start_info)?;

    responder.send()?;

    let control_handle = virtio_gpu_fidl.control_handle();

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

    let control_stream = device.take_stream(wire::CONTROLQ).context("Failed to find CONTROLQ")?;
    // Zircon doesn't configure the cursor queue, so we continue if that queue doesn't exist.
    let cursor_stream = device.take_stream(wire::CURSORQ);
    if cursor_stream.is_err() {
        tracing::warn!("Guest did not configure cursor queue; cursor support will be unavailable");
    }
    ready_responder.send()?;

    // Create the device and attempt to attach an initial scanout. This may fail, ex if run within
    // an environment without Flatland or a graphical shell. If this happens then the GPU will
    // simply not expose any scanouts to the driver.
    //
    // We put this after we reply to the ready_responder but before we process queues so that we
    // can allow the guest to continue running until it needs to interact with a virtio queue. We
    // don't process the queues until this operation is completed so that we can send some initial
    // display info to the device. The linux driver is able to handle hot-plug and display resize
    // but the zircon driver will only detect display geometry once at initialization and doesn't
    // handle the config change interrupt on resize.
    let mut gpu_device = GpuDevice::new(&guest_mem, control_handle);
    let command_sender = gpu_device.command_sender();
    let keyboard_listener =
        keyboard_listener.map(|k| ClientEnd::<KeyboardListenerMarker>::from(k.into_channel()));
    select! {
        _ = gpu_device.process_gpu_commands().fuse() => {},
        result = FlatlandScanout::attach(command_sender, keyboard_listener).fuse() => {
            if let Err(e) = result {
                tracing::warn!("Failed to create scanout: {}", e);
            }
        },
    }

    futures::try_join!(
        device
            .run_device_notify(virtio_device_fidl)
            .map_err(|e| anyhow!("run_device_notify: {}", e)),
        gpu_device.process_queues(control_stream, cursor_stream.ok()),
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
