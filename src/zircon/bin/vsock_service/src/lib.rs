// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

mod addr;
mod port;
mod service;

pub use self::service::Vsock;

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{self, Proxy},
        fidl_fuchsia_hardware_vsock::{
            CallbacksProxy, DeviceMarker, DeviceRequest, DeviceRequestStream,
        },
        fidl_fuchsia_vsock::{
            AcceptorMarker, AcceptorRequest, ConnectionMarker, ConnectionProxy,
            ConnectionTransport, ConnectorMarker, ConnectorProxy,
        },
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::{channel, future, FutureExt, StreamExt, TryFutureExt},
    };
    struct MockDriver {
        client: DeviceRequestStream,
        callbacks: CallbacksProxy,
    }

    impl MockDriver {
        fn new(client: DeviceRequestStream, callbacks: CallbacksProxy) -> Self {
            MockDriver { client, callbacks }
        }
    }

    macro_rules! unwrap_msg {
        ($msg:path{$($bindings:tt)*} from $stream:expr) => {
            if let Some(Ok($msg{$($bindings)*})) = $stream.next().await {
                ($($bindings)*)
            } else {
                panic!("Expected msg {}", stringify!($msg));
            }
        }
    }

    async fn common_setup() -> Result<(MockDriver, Vsock), anyhow::Error> {
        let (driver_client, driver_server) = endpoints::create_endpoints::<DeviceMarker>()?;
        let mut driver_server = driver_server.into_stream()?;

        // Vsock::new expects to be able to communication with a running driver instance.
        // As we don't have a driver instance we spin up an asynchronous thread that will
        // momentarily pretend to be the driver to receive the callbacks, and then send
        // those callbacks over the below oneshot channel that we can then receive after
        // Vsock::new completes.
        let (tx, rx) = channel::oneshot::channel();
        fasync::Task::spawn(async move {
            let (cb, responder) =
                unwrap_msg!(DeviceRequest::Start{cb, responder} from driver_server);
            let driver_callbacks = cb.into_proxy().unwrap();
            responder.send(zx::Status::OK.into_raw()).unwrap();
            let _ = tx.send((driver_server, driver_callbacks));
        })
        .detach();

        let (service, event_loop) = Vsock::new(driver_client.into_proxy()?).await?;
        fasync::Task::spawn(event_loop.map_err(|x| panic!("Event loop stopped {}", x)).map(|_| ()))
            .detach();
        let (driver_server, driver_callbacks) = rx.await?;
        let driver = MockDriver::new(driver_server, driver_callbacks);
        Ok((driver, service))
    }

    fn make_con() -> Result<(zx::Socket, ConnectionProxy, ConnectionTransport), anyhow::Error> {
        let (client_socket, server_socket) = zx::Socket::create(zx::SocketOpts::STREAM)?;
        let (client_end, server_end) = endpoints::create_endpoints::<ConnectionMarker>()?;
        let client_end = client_end.into_proxy()?;
        let con = ConnectionTransport { data: server_socket, con: server_end };
        Ok((client_socket, client_end, con))
    }

    fn make_client(service: &Vsock) -> Result<ConnectorProxy, anyhow::Error> {
        let (app_client, app_remote) = endpoints::create_endpoints::<ConnectorMarker>()?;
        let app_client = app_client.into_proxy()?;
        // Run the client
        fasync::Task::spawn(
            Vsock::run_client_connection(service.clone(), app_remote.into_stream()?)
                .then(|_| future::ready(())),
        )
        .detach();
        Ok(app_client)
    }

    #[fasync::run_until_stalled(test)]
    async fn basic_listen() -> Result<(), anyhow::Error> {
        let (mut driver, service) = common_setup().await?;

        let app_client = make_client(&service)?;

        // Should reject listening at the ephemeral port ranges.
        {
            let (acceptor_remote, _acceptor_client) =
                endpoints::create_endpoints::<AcceptorMarker>()?;
            assert_eq!(
                app_client.listen(49152, acceptor_remote).await?,
                zx::sys::ZX_ERR_UNAVAILABLE
            );
        }

        // Listen on a reasonable value.
        let (acceptor_remote, acceptor_client) = endpoints::create_endpoints::<AcceptorMarker>()?;
        assert_eq!(app_client.listen(8000, acceptor_remote).await?, zx::sys::ZX_OK);
        let mut acceptor_client = acceptor_client.into_stream()?;

        // Validate that we cannot listen twice
        {
            let (acceptor_remote, _acceptor_client) =
                endpoints::create_endpoints::<AcceptorMarker>()?;
            assert_eq!(
                app_client.listen(8000, acceptor_remote).await?,
                zx::sys::ZX_ERR_ALREADY_BOUND
            );
        }

        // Create a connection from the driver
        driver.callbacks.request(&mut *addr::Vsock::new(8000, 80, 4))?;
        let (_data_socket, _client_end, mut con) = make_con()?;

        let (_, responder) =
            unwrap_msg!(AcceptorRequest::Accept{addr, responder} from acceptor_client);
        responder.send(Some(&mut con))?;

        // expect a response
        let (_, _server_data_socket, responder) =
            unwrap_msg!(DeviceRequest::SendResponse{addr, data, responder} from driver.client);
        responder.send(zx::Status::OK.into_raw())?;

        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn reject_connection() -> Result<(), anyhow::Error> {
        let (mut driver, service) = common_setup().await?;

        let app_client = make_client(&service)?;

        // Send a connection request
        let (_data_socket, _client_end, mut con) = make_con()?;
        let request = app_client.connect(4, 8000, &mut con);

        // Expect a driver message
        {
            let (mut addr, _server_data_socket, responder) =
                unwrap_msg!(DeviceRequest::SendRequest{addr, data, responder} from driver.client);
            responder.send(zx::Status::OK.into_raw())?;
            // Now simulate an incoming RST for a rejected connection
            driver.callbacks.rst(&mut addr)?;
            // Leave this scope to drop the server_data_socket
        }
        // Request should resolve to an error
        let (status, _port) = request.await?;
        assert_eq!(status, zx::sys::ZX_ERR_UNAVAILABLE);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn transport_reset() -> Result<(), anyhow::Error> {
        let (mut driver, service) = common_setup().await?;

        let app_client = make_client(&service)?;

        // Create a connection.
        let (_data_socket_request, client_end_request, mut con) = make_con()?;
        let request = app_client.connect(4, 8000, &mut con);
        let (mut addr, server_data_socket_request, responder) =
            unwrap_msg!(DeviceRequest::SendRequest{addr, data, responder} from driver.client);
        responder.send(zx::Status::OK.into_raw())?;
        driver.callbacks.response(&mut addr)?;
        let (status, _) = request.await?;
        zx::Status::ok(status)?;

        // Start a listener
        let (acceptor_remote, acceptor_client) = endpoints::create_endpoints::<AcceptorMarker>()?;
        assert_eq!(app_client.listen(9000, acceptor_remote).await?, zx::sys::ZX_OK);
        let mut acceptor_client = acceptor_client.into_stream()?;

        // Perform a transport reset
        drop(server_data_socket_request);
        driver.callbacks.transport_reset(7).await?;

        // Connection should be closed
        client_end_request.on_closed().await?;

        // Listener should still be active and receive a connection
        driver.callbacks.request(&mut *addr::Vsock::new(9000, 80, 4))?;
        let (_data_socket, _client_end, _con) = make_con()?;

        let (_addr, responder) =
            unwrap_msg!(AcceptorRequest::Accept{addr, responder} from acceptor_client);
        drop(responder); // don't bother responding, we're done

        Ok(())
    }
}
