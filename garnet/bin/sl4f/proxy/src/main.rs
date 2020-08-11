// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_testing_proxy::{
    TcpProxyControlRequest, TcpProxyControlRequestStream, TcpProxy_Marker, TcpProxy_RequestStream,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::{
    channel::mpsc,
    future::{select, FutureExt, TryFutureExt},
    io::AsyncReadExt,
    lock::Mutex,
    stream::{StreamExt, TryStreamExt},
};
use log::{error, info, warn};
use std::{
    collections::HashMap,
    net::{Ipv4Addr, Ipv6Addr, SocketAddrV4, SocketAddrV6},
    sync::Arc,
};

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["tcp-proxy"]).expect("Can't init logger");
    let mut fs = ServiceFs::new();
    let proxy_control = Arc::new(TcpProxyControl::new());
    fs.dir("svc").add_fidl_service(move |stream| {
        let proxy_control_clone = proxy_control.clone();
        fasync::Task::spawn(async move {
            proxy_control_clone
                .serve_requests_from_stream(stream)
                .unwrap_or_else(|e| error!("Error handling TcpProxyControl channel: {:?}", e))
                .await;
        })
        .detach();
    });
    fs.take_and_serve_directory_handle().unwrap();
    fs.collect::<()>().await;
}

/// An implementation of `fuchsia.testing.proxy.TcpProxyControl` that opens
/// proxy ports accessible from a remote device.
struct TcpProxyControl {
    /// A mapping from target port to handles of TCP proxies.
    proxy_handles: Mutex<HashMap<u16, TcpProxyHandle>>,
}

impl TcpProxyControl {
    pub fn new() -> Self {
        TcpProxyControl { proxy_handles: Mutex::new(HashMap::new()) }
    }

    /// Serve `TcpProxyControlRequest`s recieved on the provided stream.
    pub async fn serve_requests_from_stream(
        &self,
        mut stream: TcpProxyControlRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            let TcpProxyControlRequest::OpenProxy_ { target_port, tcp_proxy, responder } = req;
            let open_port = self.open_proxy(target_port, tcp_proxy).await.map_err(|e| {
                warn!("Error opening proxy: {:?}", e);
                e
            })?;
            responder.send(open_port)?;
        }
        Ok(())
    }

    async fn open_proxy(
        &self,
        target_port: u16,
        tcp_proxy_token: ServerEnd<TcpProxy_Marker>,
    ) -> Result<u16, Error> {
        let mut tcp_proxy_stream = tcp_proxy_token.into_stream()?;
        let mut proxy_handles_lock = self.proxy_handles.lock().await;

        if let Some(proxy_handle) = proxy_handles_lock.get(&target_port) {
            match proxy_handle.register_client(tcp_proxy_stream) {
                // proxy exists and will extend it's lifetime until the client drops
                Ok(open_port) => return Ok(open_port),
                // error indicates the proxy has stopped and should be recreated.
                Err(RegisterError::ProxyClosed(stream)) => tcp_proxy_stream = stream,
            }
        }

        let (tcp_proxy, tcp_proxy_handle) = TcpProxy::new(target_port)?;
        let open_port = tcp_proxy_handle
            .register_client(tcp_proxy_stream)
            .map_err(|_| anyhow!("Error registering channel with new proxy"))?;
        fasync::Task::spawn(async move {
            info!("Forwarding port {:?} to {:?}", open_port, target_port);
            tcp_proxy.serve_proxy_while_open_clients().await;
            info!("Stopped forwarding to port {:?}", target_port);
        })
        .detach();

        proxy_handles_lock.insert(target_port, tcp_proxy_handle);
        Ok(open_port)
    }
}

/// A proxy that forwards TCP requests made to an externally accessible port to a target port which
/// is only internally available.
struct TcpProxy {
    /// Listener that accepts connections on the open port.
    tcp_listener: fasync::net::TcpListener,
    /// Target port on the local device to forward requests to.
    target_port: u16,
    /// Channel through which the `TcpProxy` is notified of new clients.
    stream_receiver: mpsc::UnboundedReceiver<TcpProxy_RequestStream>,
}

struct TcpProxyHandle {
    /// The port on which the `TcpProxy` is listening for connection requests.
    open_port: u16,
    /// Channel through which the handle notifies `TcpProxy` of new clients.
    stream_sender: mpsc::UnboundedSender<TcpProxy_RequestStream>,
}

enum RegisterError {
    ProxyClosed(TcpProxy_RequestStream),
}

impl TcpProxyHandle {
    /// Register a client with the corresponding `TcpProxy`, extending it's lifetime until the
    /// given token is closed. Returns the port the proxy listens on, or an error if the `TcpProxy`
    /// has already terminated.
    pub fn register_client(&self, token: TcpProxy_RequestStream) -> Result<u16, RegisterError> {
        match self.stream_sender.unbounded_send(token) {
            Ok(()) => Ok(self.open_port),
            Err(e) => Err(RegisterError::ProxyClosed(e.into_inner())),
        }
    }
}

impl TcpProxy {
    /// Creates a new `TcpProxy` and corresponding `TcpProxyHandle` with which to register clients.
    pub fn new(target_port: u16) -> Result<(Self, TcpProxyHandle), Error> {
        let open_addr: SocketAddrV6 = "[::]:0".parse()?;
        let tcp_listener = fasync::net::TcpListener::bind(&open_addr.into())?;
        let open_port = tcp_listener.local_addr()?.port();

        let (sender, receiver) = mpsc::unbounded();
        let proxy = TcpProxy { tcp_listener, target_port, stream_receiver: receiver };
        let proxy_handle = TcpProxyHandle { open_port, stream_sender: sender };
        Ok((proxy, proxy_handle))
    }

    /// Proxies requests received on the open port to the target port, until all the clients
    /// registered via the corresponding `TcpProxyHandle` are closed or the proxy crashes.
    async fn serve_proxy_while_open_clients(self) {
        let TcpProxy { tcp_listener, target_port, mut stream_receiver } = self;
        let clients_complete_fut = async move {
            while let Some(Some(request_stream)) = stream_receiver.next().now_or_never() {
                request_stream.collect::<Vec<_>>().await;
            }
        };
        select(clients_complete_fut.boxed(), Self::serve_proxy(tcp_listener, target_port).boxed())
            .await;
    }

    /// Proxies any requests recieved on |tcp_listener| to localhost:|target_port|.
    async fn serve_proxy(tcp_listener: fasync::net::TcpListener, target_port: u16) {
        tcp_listener
            .accept_stream()
            .try_for_each_concurrent(None, |(client_conn, _addr)| async move {
                let v6_addr = SocketAddrV6::new(Ipv6Addr::LOCALHOST, target_port, 0, 0);
                let v4_addr = SocketAddrV4::new(Ipv4Addr::LOCALHOST, target_port);
                let server_conn = match fasync::net::TcpStream::connect(v6_addr.into())?.await {
                    Ok(v6_conn) => v6_conn,
                    Err(_) => fasync::net::TcpStream::connect(v4_addr.into())?.await?,
                };
                Self::serve_single_connection(client_conn, server_conn)
                    .await
                    .unwrap_or_else(|e| warn!("Error serving tunnel: {:?}", e));
                Ok(())
            })
            .await
            .unwrap_or_else(|e| warn!("Error listening for tcp connections: {:?}", e));
    }

    /// Proxy a single connection between the provided `TcpStream`s.
    async fn serve_single_connection(
        client_conn: fasync::net::TcpStream,
        server_conn: fasync::net::TcpStream,
    ) -> Result<(), Error> {
        let (client_read, mut client_write) = client_conn.split();
        let (server_read, mut server_write) = server_conn.split();
        let client_to_server_fut = futures::io::copy(client_read, &mut server_write);
        let server_to_client_fut = futures::io::copy(server_read, &mut client_write);

        // If one side closes, the connection is broken so we can stop both sides.
        select(client_to_server_fut.boxed(), server_to_client_fut.boxed()).await.factor_first().0?;
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_testing_proxy::{TcpProxyControlMarker, TcpProxyControlProxy};
    use fuchsia_async::DurationExt;
    use fuchsia_zircon as zx;
    use futures::future::TryFutureExt;
    use hyper::server::{accept::from_stream, Server};
    use hyper::{Body, Response, Uri};
    use std::{convert::Infallible, net::SocketAddr};

    fn launch_data_proxy_control() -> TcpProxyControlProxy {
        let control = TcpProxyControl::new();
        let (proxy, stream) = create_proxy_and_stream::<TcpProxyControlMarker>().unwrap();
        fasync::Task::spawn(async move {
            control.serve_requests_from_stream(stream).await.unwrap();
        })
        .detach();
        proxy
    }

    const TEST_RESPONSE: &str = "success";
    /// Assumed time for which serving a proxy completes after the last client drops.
    const STOP_DURATION: zx::Duration = zx::Duration::from_millis(100);

    /// Launches an http server that always responds with success. Returns the port the
    /// server is listening on.
    fn launch_test_server(addr: SocketAddr) -> u16 {
        let tcp_listener = fasync::net::TcpListener::bind(&addr).unwrap();
        let port = tcp_listener.local_addr().unwrap().port();
        let connections = tcp_listener
            .accept_stream()
            .map_ok(|(conn, _addr)| fuchsia_hyper::TcpStream { stream: conn });

        let make_svc = hyper::service::make_service_fn(move |_socket| async {
            Ok::<_, Infallible>(hyper::service::service_fn(move |_req| async {
                info!("HTTP server got request!");
                Ok::<_, Infallible>(Response::new(Body::from(TEST_RESPONSE)))
            }))
        });
        let server = Server::builder(from_stream(connections))
            .executor(fuchsia_hyper::Executor)
            .serve(make_svc)
            .unwrap_or_else(|e| panic!("HTTP server failed! {:?}", e));
        fasync::Task::spawn(server).detach();

        port
    }

    fn launch_test_server_v6() -> u16 {
        let addr: SocketAddrV6 = "[::1]:0".parse().unwrap();
        launch_test_server(addr.into())
    }

    fn launch_test_server_v4() -> u16 {
        let addr: SocketAddrV4 = "127.0.0.1:0".parse().unwrap();
        launch_test_server(addr.into())
    }

    /// Asserts that an HTTP request to [::1]:port succeeds.
    async fn assert_request(port: u16) {
        let http_client = fuchsia_hyper::new_client();
        let request_uri = Uri::builder()
            .scheme("http")
            .authority(format!("[::1]:{:?}", port).as_str())
            .path_and_query("/")
            .build()
            .unwrap();
        let response = http_client.get(request_uri).await.unwrap();
        assert_eq!(response.status(), hyper::StatusCode::OK);
        let resp_body = response
            .into_body()
            .try_fold(Vec::new(), |mut vec, b| async move {
                vec.extend(b);
                Ok(vec)
            })
            .await
            .unwrap();
        assert_eq!(String::from_utf8(resp_body).unwrap().as_str(), TEST_RESPONSE);
    }

    /// Asserts that an HTTP request to [::1]:port fails.
    async fn assert_unreachable(port: u16) {
        let http_client = fuchsia_hyper::new_client();
        let request_uri = Uri::builder()
            .scheme("http")
            .authority(format!("[::1]:{:?}", port).as_str())
            .path_and_query("/")
            .build()
            .unwrap();
        assert!(http_client.get(request_uri).await.is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_tcp_proxy_lifetime() {
        let test_port = launch_test_server_v6();
        assert_request(test_port).await;

        let (tcp_proxy, tcp_proxy_handle) = TcpProxy::new(test_port).unwrap();
        let (tcp_proxy_token, tcp_proxy_server_end) =
            create_proxy_and_stream::<TcpProxy_Marker>().unwrap();
        let proxy_port = tcp_proxy_handle
            .register_client(tcp_proxy_server_end)
            .map_err(|_| anyhow!("Error on register client"))
            .unwrap();
        let tcp_proxy_fut = tcp_proxy.serve_proxy_while_open_clients().shared();
        fasync::Task::spawn(tcp_proxy_fut.clone()).detach();

        // test server reachable while proxy is served
        assert_request(proxy_port).await;
        // write again
        assert_request(proxy_port).await;

        // after dropping the TcpProxy, serving proxy should complete
        drop(tcp_proxy_token);
        tcp_proxy_fut.await;
        assert_unreachable(proxy_port).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_tcp_proxy_ipv4() {
        let test_port = launch_test_server_v4();

        let (tcp_proxy, tcp_proxy_handle) = TcpProxy::new(test_port).unwrap();
        let (tcp_proxy_token, tcp_proxy_server_end) =
            create_proxy_and_stream::<TcpProxy_Marker>().unwrap();
        let proxy_port = tcp_proxy_handle
            .register_client(tcp_proxy_server_end)
            .map_err(|_| anyhow!("Error on register client"))
            .unwrap();
        let tcp_proxy_fut = tcp_proxy.serve_proxy_while_open_clients().shared();
        fasync::Task::spawn(tcp_proxy_fut.clone()).detach();

        // test server reachable while proxy is served
        assert_request(proxy_port).await;

        // after dropping the TcpProxy, serving proxy should complete
        drop(tcp_proxy_token);
        tcp_proxy_fut.await;
        assert_unreachable(proxy_port).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_tcp_proxy_multiple_clients_lifetime() {
        let test_port = launch_test_server_v6();
        assert_request(test_port).await;

        let (tcp_proxy, tcp_proxy_handle) = TcpProxy::new(test_port).unwrap();
        let (tcp_proxy_token, tcp_proxy_server_end) =
            create_proxy_and_stream::<TcpProxy_Marker>().unwrap();
        let proxy_port = tcp_proxy_handle
            .register_client(tcp_proxy_server_end)
            .map_err(|_| anyhow!("Error on register client"))
            .unwrap();
        let tcp_proxy_fut = tcp_proxy.serve_proxy_while_open_clients().shared();
        fasync::Task::spawn(tcp_proxy_fut.clone()).detach();

        // create second client
        let (tcp_proxy_token_2, tcp_proxy_server_end_2) =
            create_proxy_and_stream::<TcpProxy_Marker>().unwrap();
        tcp_proxy_handle
            .register_client(tcp_proxy_server_end_2)
            .map_err(|_| anyhow!("Error on register client"))
            .unwrap();

        assert_request(proxy_port).await;

        // after dropping first handle, but not second, proxy is still accessible
        drop(tcp_proxy_token);
        assert_request(proxy_port).await;

        // proxy completes after second handle dropped
        drop(tcp_proxy_token_2);
        tcp_proxy_fut.await;
        assert_unreachable(proxy_port).await;
    }

    #[fasync::run_until_stalled(test)]
    async fn test_proxy_control_reuses_proxy() {
        let control_proxy = launch_data_proxy_control();
        let target_port = 80;

        let (_proxy_client, proxy_client_server_end) = create_proxy::<TcpProxy_Marker>().unwrap();
        let open_port =
            control_proxy.open_proxy_(target_port, proxy_client_server_end).await.unwrap();

        let (_proxy_client_2, proxy_client_server_end_2) =
            create_proxy::<TcpProxy_Marker>().unwrap();
        let open_port_2 =
            control_proxy.open_proxy_(target_port, proxy_client_server_end_2).await.unwrap();

        assert_eq!(open_port, open_port_2);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_proxy_control_recreate_proxy() {
        let test_port = launch_test_server_v6();
        assert_request(test_port).await;
        let control_proxy = launch_data_proxy_control();

        // echo server reachable through proxy
        let (tcp_proxy, tcp_proxy_server_end) = create_proxy().unwrap();
        let proxy_port = control_proxy.open_proxy_(test_port, tcp_proxy_server_end).await.unwrap();
        assert_request(proxy_port).await;

        // after dropping the TcpProxy, serving should stop
        std::mem::drop(tcp_proxy);
        fasync::Timer::new(STOP_DURATION.after_now()).await;
        assert_unreachable(proxy_port).await;

        // reachable after recreating proxy
        let (_tcp_proxy, tcp_proxy_server_end) = create_proxy().unwrap();
        let proxy_port = control_proxy.open_proxy_(test_port, tcp_proxy_server_end).await.unwrap();
        assert_request(proxy_port).await;
    }
}
