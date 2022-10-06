// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod connection;
mod connection_states;
mod device;
mod port_manager;
mod wire;

use {
    crate::device::VsockDevice,
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_virtualization::{HostVsockEndpointRequest, HostVsockEndpointRequestStream},
    fidl_fuchsia_virtualization_hardware::VirtioVsockRequestStream,
    fuchsia_component::server,
    fuchsia_zircon as zx,
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    std::rc::Rc,
};

// Services exposed by the Virtio Vsock device.
enum Services {
    VirtioVsockStart(VirtioVsockRequestStream),
    HostVsockEndpoint(HostVsockEndpointRequestStream),
}

async fn run_virtio_vsock(
    mut virtio_vsock_fidl: VirtioVsockRequestStream,
    vsock_device: Rc<VsockDevice>,
) -> Result<(), Error> {
    // Receive start info as first message.
    let (start_info, guest_cid, listeners, responder) = virtio_vsock_fidl
        .try_next()
        .await?
        .ok_or(anyhow!("Unexpected end of stream"))?
        .into_start()
        .ok_or(anyhow!("Expected Start message"))?;

    // Prepare the device builder from the start info. The device builder has been initialized
    // with any provided traps and notification sources.
    let (device_builder, guest_mem) = machina_virtio_device::from_start_info(start_info)?;

    if let Err(err) = vsock_device.set_guest_cid(guest_cid) {
        responder.send(&mut Err(zx::Status::INVALID_ARGS.into_raw()))?;
        return Err(err);
    }

    // Attempt to register any initial listeners before the device starts. Listeners passed this
    // way are guaranteed to be available for even the earliest guest initiated connection.
    let result = listeners.into_iter().try_for_each(|listener| {
        vsock_device.listen(
            listener.port,
            listener.acceptor.into_proxy().map_err(|_| zx::Status::BAD_HANDLE)?,
        )
    });
    if result.is_err() {
        responder.send(&mut result.map_err(|status| status.into_raw()))?;
        return result.map_err(|err| anyhow!("Failed to register initial listener: {}", err));
    }

    // Acknowledge that StartInfo was correct by responding to the controller.
    responder.send(&mut Ok(()))?;

    // Complete the setup of queues and get a virtio device.
    let mut virtio_device_fidl = virtio_vsock_fidl.cast_stream();
    let (device, ready_responder) = machina_virtio_device::config_builder_from_stream(
        device_builder,
        &mut virtio_device_fidl,
        &[wire::RX_QUEUE_IDX, wire::TX_QUEUE_IDX, wire::EVENT_QUEUE_IDX][..],
        &guest_mem,
    )
    .await?;

    let tx_stream = device.take_stream(wire::TX_QUEUE_IDX)?;
    let rx_stream = device.take_stream(wire::RX_QUEUE_IDX)?;

    // Ignore the event queue as we don't support VM migrations.
    let _event_stream = device.take_stream(wire::EVENT_QUEUE_IDX)?;

    // Notify the controller that vsock is ready.
    ready_responder.send()?;

    futures::try_join!(
        device
            .run_device_notify(virtio_device_fidl)
            .map_err(|e| anyhow!("run_device_notify: {}", e)),
        vsock_device.handle_tx_stream(tx_stream, &guest_mem),
        vsock_device.handle_rx_stream(rx_stream, &guest_mem),
    )?;

    Ok(())
}

async fn handle_host_vsock_endpoint(
    host_endpoint_fidl: HostVsockEndpointRequestStream,
    vsock_device: Rc<VsockDevice>,
) -> Result<(), Error> {
    host_endpoint_fidl
        .try_for_each_concurrent(None, |request| async {
            match request {
                HostVsockEndpointRequest::Listen { port, acceptor, responder } => responder.send(
                    &mut vsock_device
                        .listen(port, acceptor.into_proxy()?)
                        .map_err(|err| err.into_raw()),
                ),
                HostVsockEndpointRequest::Connect { guest_port, responder } => {
                    vsock_device.client_initiated_connect(guest_port, responder).await
                }
            }
        })
        .await
        .map_err(|err| anyhow!(err))
}

#[fuchsia::main(logging = true, logging_minimum_severity = "debug", threads = 1)]
async fn main() -> Result<(), Error> {
    let vsock_device = VsockDevice::new();

    let mut fs = server::ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(Services::VirtioVsockStart)
        .add_fidl_service(Services::HostVsockEndpoint);
    fs.take_and_serve_directory_handle().context("Error starting server")?;
    fs.for_each_concurrent(None, |request| async {
        if let Err(err) = match request {
            Services::VirtioVsockStart(stream) => {
                run_virtio_vsock(stream, vsock_device.clone()).await
            }
            Services::HostVsockEndpoint(stream) => {
                handle_host_vsock_endpoint(stream, vsock_device.clone()).await
            }
        } {
            tracing::info!(%err, "Stopping virtio vsock service");
        }
    })
    .await;

    Ok(())
}
