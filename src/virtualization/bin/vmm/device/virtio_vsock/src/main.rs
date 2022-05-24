// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod connection;
mod device;
mod port_manager;
mod wire;

use {
    crate::device::VsockDevice,
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_virtualization::GuestVsockEndpointRequestStream,
    fidl_fuchsia_virtualization_hardware::VirtioVsockRequestStream,
    fuchsia_component::server,
    fuchsia_syslog as syslog, fuchsia_zircon as zx,
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    std::rc::Rc,
    virtio_device::chain::ReadableChain,
};

// Services exposed by the Virtio Vsock device.
enum Services {
    VirtioVsockStart(VirtioVsockRequestStream),
    GuestVsockEndpoint(GuestVsockEndpointRequestStream),
}

async fn run_virtio_vsock(
    mut virtio_vsock_fidl: VirtioVsockRequestStream,
    vsock_device: Rc<VsockDevice>,
) -> Result<(), Error> {
    // Receive start info as first message.
    let (start_info, guest_cid, responder) = virtio_vsock_fidl
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
    .await
    .context("Failed to initialize device")?;

    let tx_stream = device.take_stream(wire::TX_QUEUE_IDX)?;

    // TODO(fxb/97355): Implement the RX queue.
    let _rx_stream = device.take_stream(wire::RX_QUEUE_IDX)?;

    // Ignore the event queue as we don't support VM migrations.
    let _ = device.take_stream(wire::EVENT_QUEUE_IDX)?;

    // Notify the controller that vsock is ready.
    ready_responder.send()?;

    futures::try_join!(
        device
            .run_device_notify(virtio_device_fidl)
            .map_err(|e| anyhow!("run_device_notify: {}", e)),
        tx_stream.map(|chain| Ok((chain, vsock_device.clone()))).try_for_each_concurrent(None, {
            let guest_mem = &guest_mem;
            move |(chain, device)| async move {
                device.handle_tx_queue(ReadableChain::new(chain, guest_mem)).await
            }
        }),
    )?;

    Ok(())
}

async fn handle_guest_vsock_endpoint(
    mut guest_endpoint_fidl: GuestVsockEndpointRequestStream,
    vsock_device: Rc<VsockDevice>,
) -> Result<(), Error> {
    let (guest_cid, host_conn, guest_acceptor, control_handle) = guest_endpoint_fidl
        .try_next()
        .await?
        .ok_or(anyhow!("Unexpected end of stream"))?
        .into_set_context_id()
        .ok_or(anyhow!("Expected set context id message"))?;

    vsock_device
        .legacy_set_context_id(
            guest_cid,
            host_conn.into_proxy()?,
            guest_acceptor.into_stream()?,
            control_handle,
        )
        .await
}

async fn serve_fidl_endpoints() -> Result<(), Error> {
    let vsock_device = VsockDevice::new();

    let mut fs = server::ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(Services::VirtioVsockStart)
        .add_fidl_service(Services::GuestVsockEndpoint);
    fs.take_and_serve_directory_handle().context("Error starting server")?;
    fs.for_each_concurrent(None, |request| async {
        if let Err(err) = match request {
            Services::VirtioVsockStart(stream) => {
                run_virtio_vsock(stream, vsock_device.clone()).await
            }
            Services::GuestVsockEndpoint(stream) => {
                handle_guest_vsock_endpoint(stream, vsock_device.clone()).await
            }
        } {
            syslog::fx_log_err!("Error running virtio vsock service: {}", err);
        }
    })
    .await;

    Ok(())
}

#[fuchsia::main(logging = true, threads = 1)]
async fn main() -> Result<(), Error> {
    serve_fidl_endpoints().await
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_endpoints, create_proxy_and_stream},
        fidl_fuchsia_virtualization::{
            GuestVsockAcceptorMarker, GuestVsockEndpointMarker, HostVsockConnectorMarker,
        },
    };

    #[fuchsia::test]
    async fn serve_guest_vsock_endpoint() {
        let vsock_device = VsockDevice::new();
        let (proxy, stream) = create_proxy_and_stream::<GuestVsockEndpointMarker>()
            .expect("Failed to create GuestVsockEndpoint proxy and stream");

        let (host_conn_client, _) = create_endpoints::<HostVsockConnectorMarker>()
            .expect("Failed to create HostVsockConnector endpoints");
        let (_, guest_acceptor_sever) = create_endpoints::<GuestVsockAcceptorMarker>()
            .expect("Failed to create GuestVsockAcceptor endpoints");

        // Simple check to validate that device handled the GuestVsockEndpoint message by checking that
        // the default guest CID was updated.
        let expected_guest_cid = 12345;
        proxy.set_context_id(expected_guest_cid, host_conn_client, guest_acceptor_sever).unwrap();
        handle_guest_vsock_endpoint(stream, vsock_device.clone())
            .await
            .expect("Failed to handle guest vsock endpoint");

        assert_eq!(vsock_device.clone().guest_cid(), expected_guest_cid);
    }
}
