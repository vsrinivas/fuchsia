// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{Context as _, Result},
    async_net::TcpListener,
    async_trait::async_trait,
    fidl, fidl_fuchsia_developer_bridge as bridge, fidl_fuchsia_developer_remotecontrol as rcs,
    fidl_fuchsia_net::SocketAddress,
    fidl_fuchsia_net_ext::SocketAddress as SocketAddressExt,
    futures::{future::join, AsyncReadExt as _, AsyncWriteExt as _, StreamExt as _},
    protocols::prelude::*,
    serde::{Deserialize, Serialize},
    serde_json::{self, Value},
    std::sync::Arc,
};

#[ffx_protocol]
#[derive(Default)]
pub struct Forward(Arc<tasks::TaskManager>);

#[derive(Deserialize, Serialize)]
enum ForwardConfigType {
    Tcp,
}

#[derive(Deserialize, Serialize)]
struct ForwardConfig {
    #[serde(rename = "type")]
    ty: ForwardConfigType,
    target: String,
    host_address: std::net::SocketAddr,
    target_address: std::net::SocketAddr,
}

const TUNNEL_CFG: &'static str = "tunnels";

impl Forward {
    async fn port_forward_task(
        cx: Context,
        target: String,
        mut target_address: SocketAddress,
        listener: TcpListener,
        tasks: Arc<tasks::TaskManager>,
    ) {
        let mut incoming = listener.incoming();
        while let Some(conn) = incoming.next().await {
            let conn = match conn {
                Ok(conn) => conn,
                Err(e) => {
                    log::error!("Error accepting connection for TCP forwarding: {:?}", e);
                    continue;
                }
            };

            let target = match cx.open_remote_control(Some(target.clone())).await {
                Ok(t) => t,
                Err(e) => {
                    log::error!("Could not connect to proxy for TCP forwarding: {:?}", e);
                    break;
                }
            };

            let (socket, remote) = match fidl::Socket::create(fidl::SocketOpts::STREAM) {
                Ok(x) => x,
                Err(e) => {
                    log::error!("Error creating socket: {:?}", e);
                    continue;
                }
            };

            match target.forward_tcp(&mut target_address, remote).await {
                Ok(Ok(())) => (),
                Ok(Err(e)) => {
                    log::error!("Error forwarding port: {:?}", e);
                    continue;
                }
                Err(e) => {
                    log::error!("Error requesting port forward from RCS: {:?}", e);
                    continue;
                }
            }

            let socket = match fuchsia_async::Socket::from_socket(socket) {
                Ok(socket) => socket,
                Err(e) => {
                    log::error!("Error converting socket to async: {:?}", e);
                    continue;
                }
            };

            Self::establish_tcp_forward(conn, socket, Arc::clone(&tasks));
        }
    }

    async fn bind_or_log(addr: std::net::SocketAddr) -> Result<TcpListener, ()> {
        TcpListener::bind(addr).await.map_err(|e| {
            log::error!("Could not listen on {:?}: {:?}", addr, e);
        })
    }

    fn establish_tcp_forward(
        conn: async_net::TcpStream,
        socket: fuchsia_async::Socket,
        tasks: Arc<tasks::TaskManager>,
    ) {
        let (mut socket_read, mut socket_write) = socket.split();
        let (mut conn_read, mut conn_write) = conn.split();

        let write_read = async move {
            let mut buf = [0; 4096];
            loop {
                let bytes = socket_read.read(&mut buf).await?;
                if bytes == 0 {
                    break Ok(());
                }
                conn_write.write_all(&mut buf[..bytes]).await?;
                conn_write.flush().await?;
            }
        };
        let read_write = async move {
            // TODO(84188): Use a buffer pool once we have them.
            let mut buf = [0; 4096];
            loop {
                let bytes = conn_read.read(&mut buf).await?;
                if bytes == 0 {
                    break Ok(()) as Result<(), std::io::Error>;
                }
                socket_write.write_all(&mut buf[..bytes]).await?;
                socket_write.flush().await?;
            }
        };
        let forward = join(read_write, write_read);
        tasks.spawn(async move {
            match forward.await {
                (Err(a), Err(b)) => {
                    log::warn!("Port forward closed with errors:\n  {:?}\n  {:?}", a, b)
                }
                (Err(e), _) | (_, Err(e)) => {
                    log::warn!("Port forward closed with error: {:?}", e)
                }
                _ => (),
            }
        });
    }
}

#[async_trait(?Send)]
impl FidlProtocol for Forward {
    type Protocol = bridge::TunnelMarker;
    type StreamHandler = FidlInstancedStreamHandler<Self>;

    async fn handle(&self, cx: &Context, req: bridge::TunnelRequest) -> Result<()> {
        let cx = cx.clone();

        match req {
            bridge::TunnelRequest::ForwardPort {
                target,
                host_address,
                target_address,
                responder,
            } => {
                let host_address: SocketAddressExt = host_address.into();
                let host_address = host_address.0;
                let target_address_cfg: SocketAddressExt = target_address.clone().into();
                let target_address_cfg = target_address_cfg.0;
                let listener = match Self::bind_or_log(host_address).await {
                    Ok(t) => t,
                    Err(_) => {
                        return responder
                            .send(&mut Err(bridge::TunnelError::CouldNotListen))
                            .context("error sending response");
                    }
                };

                let tasks = Arc::clone(&self.0);
                self.0.spawn(Self::port_forward_task(
                    cx,
                    target.clone(),
                    target_address,
                    listener,
                    tasks,
                ));

                let cfg = serde_json::to_value(ForwardConfig {
                    ty: ForwardConfigType::Tcp,
                    target,
                    host_address,
                    target_address: target_address_cfg,
                })?;

                if let Err(e) =
                    ffx_config::add((TUNNEL_CFG, ffx_config::ConfigLevel::User), cfg).await
                {
                    log::warn!("Failed to persist tunnel configuration: {:?}", e);
                }

                responder.send(&mut Ok(())).context("error sending response")?;
                Ok(())
            }
            bridge::TunnelRequest::ReversePort {
                target,
                host_address,
                mut target_address,
                responder,
            } => {
                let target = match cx.open_remote_control(Some(target)).await {
                    Ok(t) => t,
                    Err(e) => {
                        log::error!("Could not connect to proxy for TCP forwarding: {:?}", e);
                        return responder
                            .send(&mut Err(bridge::TunnelError::TargetConnectFailed))
                            .context("error sending response");
                    }
                };

                let (client, mut event_stream) =
                    fidl::endpoints::create_request_stream::<rcs::ForwardCallbackMarker>()?;
                match target.reverse_tcp(&mut target_address, client).await? {
                    Ok(()) => (),
                    Err(e) => {
                        log::error!("Could not establish reverse TCP forward: {:?}", e);
                        return responder
                            .send(&mut Err(bridge::TunnelError::CouldNotListen))
                            .context("error sending response");
                    }
                }

                let host_address: SocketAddressExt = host_address.into();
                let host_address = host_address.0;
                let tasks = Arc::clone(&self.0);
                self.0.spawn(async move {
                    while let Some(result) = event_stream.next().await {
                        let request = match result {
                            Ok(request) => request,
                            Err(e) => {
                                log::warn!("Error reported to forwarding callback: {:?}", e);
                                continue;
                            }
                        };

                        match request {
                            rcs::ForwardCallbackRequest::Forward {
                                socket,
                                addr,
                                control_handle: _,
                            } => {
                                let addr: SocketAddressExt = addr.into();
                                let addr = addr.0;
                                log::info!(
                                    "Connection from {:?} forwarding to {:?}",
                                    addr,
                                    host_address
                                );
                                let tcp_stream =
                                    match async_net::TcpStream::connect(&host_address).await {
                                        Ok(stream) => stream,
                                        Err(e) => {
                                            log::error!(
                                                "Could not connect to {:?}: {:?}",
                                                host_address,
                                                e
                                            );
                                            continue;
                                        }
                                    };

                                let socket = match fuchsia_async::Socket::from_socket(socket) {
                                    Ok(socket) => socket,
                                    Err(e) => {
                                        log::error!("Error converting socket to async: {:?}", e);
                                        continue;
                                    }
                                };

                                Self::establish_tcp_forward(tcp_stream, socket, Arc::clone(&tasks))
                            }
                        }
                    }
                });

                responder.send(&mut Ok(())).context("error sending response")?;
                Ok(())
            }
        }
    }

    async fn start(&mut self, cx: &Context) -> Result<()> {
        log::info!("started port forwarding protocol");

        let tunnels: Vec<Value> = ffx_config::get(TUNNEL_CFG).await.unwrap_or_else(|_| Vec::new());

        for tunnel in tunnels {
            let tunnel: ForwardConfig = match serde_json::from_value(tunnel) {
                Ok(tunnel) => tunnel,
                Err(e) => {
                    log::warn!("Malformed tunnel config: {:?}", e);
                    continue;
                }
            };

            match tunnel.ty {
                ForwardConfigType::Tcp => {
                    let target_address = SocketAddressExt(tunnel.target_address);
                    let listener = match Self::bind_or_log(tunnel.host_address).await {
                        Ok(t) => t,
                        Err(_) => continue,
                    };
                    let tasks = Arc::clone(&self.0);
                    self.0.spawn(Self::port_forward_task(
                        cx.clone(),
                        tunnel.target,
                        target_address.into(),
                        listener,
                        tasks,
                    ));
                }
            }
        }
        Ok(())
    }

    async fn stop(&mut self, _cx: &Context) -> Result<()> {
        log::info!("stopped port forwarding protocol");
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {fidl_fuchsia_diagnostics as diagnostics, protocols::testing::FakeDaemonBuilder};

    #[derive(Default, Clone)]
    struct TestDaemon;

    #[async_trait(?Send)]
    impl DaemonProtocolProvider for TestDaemon {
        async fn open_remote_control(
            &self,
            target_identifier: Option<String>,
        ) -> Result<rcs::RemoteControlProxy> {
            let (client, server) =
                fidl::endpoints::create_endpoints::<rcs::RemoteControlMarker>().unwrap();
            assert_eq!(target_identifier, Some("dummy_target".to_owned()));

            fuchsia_async::Task::local(async move {
                let mut server = server.into_stream().unwrap();
                while let Some(request) = server.next().await {
                    match request.unwrap() {
                        rcs::RemoteControlRequest::ReverseTcp { addr, client: _, responder } => {
                            assert_eq!(
                                SocketAddressExt("127.0.0.1:5678".parse().unwrap()),
                                SocketAddressExt::from(addr)
                            );
                            responder.send(&mut Ok(())).unwrap();
                        }
                        other => panic!("Unexpected request: {:?}", other),
                    }
                }
            })
            .detach();

            Ok(client.into_proxy().unwrap())
        }

        async fn open_protocol(&self, _name: String) -> Result<fidl::Channel> {
            unimplemented!()
        }

        async fn open_target_proxy(
            &self,
            _target_identifier: Option<String>,
            _protocol_selector: diagnostics::Selector,
        ) -> Result<fidl::Channel> {
            unimplemented!()
        }

        async fn open_target_proxy_with_info(
            &self,
            _target_identifier: Option<String>,
            _protocol_selector: diagnostics::Selector,
        ) -> Result<(bridge::TargetInfo, fidl::Channel)> {
            unimplemented!()
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_forward() {
        let daemon = FakeDaemonBuilder::new().register_fidl_protocol::<Forward>().build();
        let _proxy = daemon.open_proxy::<bridge::TunnelMarker>().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reverse() {
        let forward = Forward::default();
        let context = Context::new(TestDaemon);
        let (client, server) = fidl::endpoints::create_endpoints::<bridge::TunnelMarker>().unwrap();

        fuchsia_async::Task::local(async move {
            let mut server = server.into_stream().unwrap();
            while let Some(request) = server.next().await {
                let request = request.unwrap();
                forward.handle(&context, request).await.unwrap();
            }
        })
        .detach();

        client
            .into_proxy()
            .unwrap()
            .reverse_port(
                "dummy_target",
                &mut SocketAddressExt("127.0.0.1:1234".parse().unwrap()).into(),
                &mut SocketAddressExt("127.0.0.1:5678".parse().unwrap()).into(),
            )
            .await
            .unwrap()
            .unwrap();
    }
}
