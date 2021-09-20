// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{Context as _, Result},
    async_net::TcpListener,
    async_trait::async_trait,
    fidl, fidl_fuchsia_developer_bridge as bridge,
    fidl_fuchsia_net::SocketAddress,
    fidl_fuchsia_net_ext::SocketAddress as SocketAddressExt,
    futures::{future::join, AsyncReadExt as _, AsyncWriteExt as _, StreamExt as _},
    services::prelude::*,
};

#[ffx_service]
#[derive(Default)]
pub struct Forward;

impl Forward {
    async fn port_forward_task(
        cx: Context,
        target: Option<String>,
        mut target_address: SocketAddress,
        listener: TcpListener,
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

            let target = match cx.open_remote_control(target.clone()).await {
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
            fuchsia_async::Task::local(async move {
                match forward.await {
                    (Err(a), Err(b)) => {
                        log::warn!("Port forward closed with errors:\n  {:?}\n  {:?}", a, b)
                    }
                    (Err(e), _) | (_, Err(e)) => {
                        log::warn!("Port forward closed with error: {:?}", e)
                    }
                    _ => (),
                }
            })
            .detach();
        }
    }
}

#[async_trait(?Send)]
impl FidlService for Forward {
    type Service = bridge::TunnelMarker;
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
                let listener = match TcpListener::bind(host_address).await {
                    Ok(t) => t,
                    Err(e) => {
                        log::error!("Could not listen on {:?}: {:?}", host_address, e);
                        return responder
                            .send(&mut Err(bridge::TunnelError::CouldNotListen))
                            .context("error sending response");
                    }
                };

                fuchsia_async::Task::local(Self::port_forward_task(
                    cx,
                    target,
                    target_address,
                    listener,
                ))
                .detach();

                responder.send(&mut Ok(())).context("error sending response")?;
                Ok(())
            }
        }
    }

    async fn start(&mut self, _cx: &Context) -> Result<()> {
        log::info!("started port forwarding service");
        Ok(())
    }

    async fn stop(&mut self, _cx: &Context) -> Result<()> {
        log::info!("stopped port forwarding service");
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use services::testing::FakeDaemonBuilder;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_forward() {
        let daemon = FakeDaemonBuilder::new().register_fidl_service::<Forward>().build();
        let _proxy = daemon.open_proxy::<bridge::TunnelMarker>().await;
    }
}
