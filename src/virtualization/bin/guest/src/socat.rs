// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{arguments, services},
    anyhow::anyhow,
    anyhow::Context,
    anyhow::Error,
    fidl_fuchsia_virtualization::{
        GuestStatus, HostVsockAcceptorMarker, HostVsockEndpointMarker, HostVsockEndpointProxy,
    },
    fuchsia_zircon as zx, fuchsia_zircon_status as zx_status,
    futures::TryStreamExt,
};

pub async fn connect_to_vsock_endpoint(
    guest_type: arguments::GuestType,
) -> Result<HostVsockEndpointProxy, Error> {
    let guest_manager = services::connect_to_manager(guest_type)?;
    let guest_info = guest_manager.get_info().await?;
    if guest_info.guest_status.unwrap() == GuestStatus::Running {
        let guest_endpoint = services::connect(guest_type).await?;
        let (vsock_endpoint, vsock_server_end) =
            fidl::endpoints::create_proxy::<HostVsockEndpointMarker>()
                .context("failed to make vsock endpoint")?;
        guest_endpoint
            .get_host_vsock_endpoint(vsock_server_end)
            .await?
            .map_err(|err| anyhow!("failed to get HostVsockEndpoint: {:?}", err))?;
        Ok(vsock_endpoint)
    } else {
        Err(anyhow!(zx_status::Status::NOT_CONNECTED))
    }
}

pub async fn handle_socat_listen(
    vsock_endpoint: HostVsockEndpointProxy,
    host_port: u32,
) -> Result<(), Error> {
    let (vsock_accept_client, mut vsock_acceptor_stream) =
        fidl::endpoints::create_request_stream::<HostVsockAcceptorMarker>()
            .context("failed to make vsock acceptor")?;

    vsock_endpoint
        .listen(host_port, vsock_accept_client)
        .await?
        .map_err(|val| zx::Status::from_raw(val))?;

    // Try_next returns a Result<Option<T>, Err>, hence we need to unwrap our value
    let connection =
        vsock_acceptor_stream.try_next().await?.ok_or(zx_status::Status::UNAVAILABLE)?;
    let (_src_cid, _src_port, port, responder) =
        connection.into_accept().ok_or(zx_status::Status::CONNECTION_REFUSED)?;

    if port != host_port {
        responder.send(&mut Err(zx_status::Status::CONNECTION_REFUSED.into_raw()))?;
        return Err(anyhow!(zx_status::Status::CONNECTION_REFUSED));
    }

    let (socket, remote_socket) = zx::Socket::create(zx::SocketOpts::STREAM)?;
    responder.send(&mut Ok(remote_socket))?;
    let io = services::GuestConsole::new(socket)?;

    io.run_with_stdio().await.map_err(From::from)
}

pub async fn handle_socat(vsock_endpoint: HostVsockEndpointProxy, port: u32) -> Result<(), Error> {
    let socket = vsock_endpoint.connect(port).await?.map_err(|val| zx::Status::from_raw(val))?;
    let io = services::GuestConsole::new(socket)?;
    io.run_with_stdio().await.map_err(From::from)
}

#[cfg(test)]
mod test {
    use {
        super::*, assert_matches::assert_matches, fidl::endpoints::create_proxy_and_stream,
        fuchsia_async as fasync, futures::future::join, futures::StreamExt,
        pretty_assertions::assert_eq,
    };

    #[fasync::run_until_stalled(test)]
    async fn socat_listen_invalid_host_returns_err() {
        let (proxy, mut stream) = create_proxy_and_stream::<HostVsockEndpointMarker>().unwrap();
        let server = async move {
            let (port, _acceptor, responder) = stream
                .next()
                .await
                .expect("Failed to read from stream")
                .expect("Failed to parse request")
                .into_listen()
                .expect("Unexpected call to Guest Proxy");
            assert_eq!(port, 0);
            responder
                .send(&mut Err(zx_status::Status::CONNECTION_REFUSED.into_raw()))
                .expect("Failed to send status code to client");
        };

        let client = handle_socat_listen(proxy, 0);
        let (_server_res, client_res) = join(server, client).await;
        assert_matches!(
            client_res.unwrap_err().downcast(),
            Ok(zx_status::Status::CONNECTION_REFUSED)
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn socat_listen_mismatched_ports_returns_err() {
        let (proxy, mut stream) = create_proxy_and_stream::<HostVsockEndpointMarker>().unwrap();
        let server = async move {
            let (port, acceptor, responder) = stream
                .next()
                .await
                .expect("Failed to read from stream")
                .expect("Failed to parse request")
                .into_listen()
                .expect("Unexpected call to Guest Proxy");
            assert_eq!(port, 0);
            responder.send(&mut Ok(())).expect("Failed to send status code to client");
            let _ = acceptor
                .into_proxy()
                .expect("Failed to convert client end into proxy")
                .accept(0, 0, 1)
                .await
                .expect("Failed to accept listener");
        };

        let client = handle_socat_listen(proxy, 0);
        let (_server_res, client_res) = join(server, client).await;
        assert_matches!(
            client_res.unwrap_err().downcast(),
            Ok(zx_status::Status::CONNECTION_REFUSED)
        );
    }
}
