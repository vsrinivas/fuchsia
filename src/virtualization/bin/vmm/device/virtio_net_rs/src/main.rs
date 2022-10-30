// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod device;
mod guest_ethernet;
mod interface;
mod wire;

use {
    anyhow::{anyhow, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_virtualization_hardware::VirtioNetRequestStream,
    fuchsia_component::server,
    futures::{StreamExt, TryFutureExt, TryStreamExt},
};

async fn run_virtio_net(mut virtio_net_fidl: VirtioNetRequestStream) -> Result<(), Error> {
    // Receive start info as first message.
    let (start_info, mac_address, enable_bridge, responder) = virtio_net_fidl
        .try_next()
        .await?
        .ok_or(anyhow!("Unexpected end of stream"))?
        .into_start()
        .ok_or(anyhow!("Expected Start message"))?;

    // Prepare the device builder from the start info. The device builder has been initialized
    // with any provided traps and notification sources.
    let (device_builder, guest_mem) = machina_virtio_device::from_start_info(start_info)?;

    // Initialize configures the netstack using the C++ dispatch loop, which is in another thread.
    // Calling Ready lets the C++ thread synchronize configuration completion with this main Rust
    // thread.
    let mut net_device = device::NetDevice::<guest_ethernet::GuestEthernet>::new()?;
    net_device.initialize(mac_address, enable_bridge).await?;

    // TODO(fxbug.dev/95485): Return a status if device creation fails.
    responder.send()?;

    // Complete the setup of queues and get a virtio device.
    let mut virtio_device_fidl = virtio_net_fidl.cast_stream();
    let (device, ready_responder) = machina_virtio_device::config_builder_from_stream(
        device_builder,
        &mut virtio_device_fidl,
        &[wire::RX_QUEUE_IDX, wire::TX_QUEUE_IDX][..],
        &guest_mem,
    )
    .await?;

    let rx_stream = device.take_stream(wire::RX_QUEUE_IDX)?;
    let tx_stream = device.take_stream(wire::TX_QUEUE_IDX)?;

    // Notify the controller that the console device is ready.
    ready_responder.send()?;

    futures::try_join!(
        device
            .run_device_notify(virtio_device_fidl)
            .map_err(|e| anyhow!("run_device_notify: {}", e)),
        net_device.get_error_from_guest_ethernet(),
        net_device.handle_tx_stream(tx_stream, &guest_mem),
        net_device.handle_rx_stream(rx_stream, &guest_mem),
    )?;

    Ok(())
}

#[fuchsia::main(logging = true, threads = 1)]
async fn main() -> Result<(), Error> {
    let mut fs = server::ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream: VirtioNetRequestStream| stream);
    fs.take_and_serve_directory_handle()
        .map_err(|err| anyhow!("Error starting server: {}", err))?;

    fs.for_each_concurrent(None, |stream| async {
        if let Err(err) = run_virtio_net(stream).await {
            tracing::info!(%err, "Stopping virtio net device");
        }
    })
    .await;

    Ok(())
}
