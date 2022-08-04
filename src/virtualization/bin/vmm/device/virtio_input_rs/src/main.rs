// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod input_device;
mod keyboard;
mod wire;

use {
    crate::input_device::InputDevice,
    anyhow::{anyhow, Context},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_virtualization_hardware::{
        KeyboardListenerRequestStream, VirtioInputRequestStream,
    },
    fuchsia_component::server,
    futures::{channel::mpsc, SinkExt, StreamExt, TryFutureExt, TryStreamExt},
    std::cell::RefCell,
    tracing,
};

async fn run_virtio_input(
    mut virtio_input_fidl: VirtioInputRequestStream,
    receiver: mpsc::Receiver<KeyboardListenerRequestStream>,
) -> Result<(), anyhow::Error> {
    // Receive start info as first message.
    let (start_info, responder) = virtio_input_fidl
        .try_next()
        .await?
        .ok_or(anyhow!("Failed to read fidl message from the channel."))?
        .into_start()
        .ok_or(anyhow!("Start should be the first message sent."))?;

    // Prepare the device builder
    let (device_builder, guest_mem) = machina_virtio_device::from_start_info(start_info)?;

    responder.send()?;

    // Complete the setup of queues and get a device.
    let mut virtio_device_fidl = virtio_input_fidl.cast_stream();
    let (device, ready_responder) = machina_virtio_device::config_builder_from_stream(
        device_builder,
        &mut virtio_device_fidl,
        &[wire::EVENTQ, wire::STATUSQ][..],
        &guest_mem,
    )
    .await
    .context("Failed to initialize device.")?;

    let mut input_device = InputDevice::new(
        &guest_mem,
        receiver,
        device.take_stream(wire::EVENTQ)?,
        device.take_stream(wire::STATUSQ).ok(),
    );
    ready_responder.send()?;

    futures::try_join!(
        device
            .run_device_notify(virtio_device_fidl)
            .map_err(|e| anyhow!("run_device_notify: {}", e)),
        input_device.run(),
    )?;
    Ok(())
}

enum IncomingService {
    VirtioInput(VirtioInputRequestStream),
    KeyboardListener(KeyboardListenerRequestStream),
}

#[fuchsia::main(logging = true, threads = 1)]
async fn main() -> Result<(), anyhow::Error> {
    let mut fs = server::ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(IncomingService::VirtioInput)
        .add_fidl_service(IncomingService::KeyboardListener);
    fs.take_and_serve_directory_handle().context("Error starting server")?;

    let (sender, receiver) = mpsc::channel::<KeyboardListenerRequestStream>(1);
    let receiver = RefCell::new(Some(receiver));
    fs.for_each_concurrent(None, |service| async {
        match service {
            IncomingService::VirtioInput(stream) => {
                if let Some(receiver) = receiver.borrow_mut().take() {
                    if let Err(e) = run_virtio_input(stream, receiver).await {
                        tracing::error!("Error running virtio_input service: {}", e);
                    }
                } else {
                    tracing::error!("virtio-input supports only a single connection");
                    return;
                }
            }
            IncomingService::KeyboardListener(stream) => sender.clone().send(stream).await.unwrap(),
        }
    })
    .await;
    Ok(())
}
