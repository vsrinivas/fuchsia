// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    async_lock::RwLock,
    fidl_fuchsia_developer_remotecontrol as rcs,
    fidl_fuchsia_net_ext::SocketAddress as SocketAddressExt,
    fuchsia_async as fasync,
    futures::{channel::mpsc::UnboundedSender, StreamExt as _},
    pkg::repository::ConnectionStream,
    protocols::prelude::*,
    std::{collections::HashMap, net::SocketAddr, sync::Arc, time::Duration},
};

const TUNNEL_CONNECT_ATTEMPTS: usize = 5;
const TUNNEL_CONNECT_RETRY_TIMEOUT: Duration = Duration::from_secs(5);

/// Manage all the repository tunnels.
#[derive(Debug)]
pub(crate) struct TunnelManager {
    tunnel_addr: SocketAddr,
    server_sink: UnboundedSender<Result<ConnectionStream>>,
    tunnels: Arc<RwLock<HashMap<String, fasync::Task<()>>>>,
}

impl TunnelManager {
    /// Create a new [TunnelManager].
    pub(crate) fn new(
        tunnel_addr: SocketAddr,
        server_sink: UnboundedSender<Result<ConnectionStream>>,
    ) -> Self {
        Self { tunnel_addr, server_sink, tunnels: Arc::new(RwLock::new(HashMap::new())) }
    }

    /// Spawn a repository tunnel to `target_nodename`.
    pub(crate) async fn start_tunnel(&self, cx: &Context, target_nodename: String) -> Result<()> {
        // Exit early if we already have a tunnel set up for this source.
        {
            let tunnels = self.tunnels.read().await;
            if tunnels.get(&target_nodename).is_some() {
                return Ok(());
            }
        }

        log::info!("creating repository tunnel for target {:?}", target_nodename);

        let tunnel_stream = create_tunnel_stream(cx, &target_nodename, self.tunnel_addr).await?;

        log::info!("created repository tunnel for target {:?}", target_nodename);

        let target_nodename_task = target_nodename.clone();
        let tunnels_task = Arc::clone(&self.tunnels);
        let server_sink_task = self.server_sink.clone();

        let tunnel_fut = async move {
            run_tunnel_protocol(&target_nodename_task, tunnel_stream, server_sink_task).await;

            // Remove the tunnel once the protocol has stopped.
            tunnels_task.write().await.remove(&target_nodename_task);
        };

        // Spawn the tunnel.
        {
            let mut tunnels = self.tunnels.write().await;

            // Check if some other task managed to spawn a tunnel before us.
            if tunnels.get(&target_nodename).is_some() {
                return Ok(());
            }

            // Otherwise, spawn the tunnel.
            let task = fasync::Task::local(tunnel_fut);
            tunnels.insert(target_nodename, task);
        }

        Ok(())
    }
}

async fn create_tunnel_stream(
    cx: &Context,
    target_nodename: &str,
    tunnel_addr: SocketAddr,
) -> Result<rcs::ForwardCallbackRequestStream> {
    let rc = cx.open_remote_control(Some(target_nodename.to_string())).await?;

    for attempt in 0..TUNNEL_CONNECT_ATTEMPTS {
        log::debug!(
            "attempt {} to create repository tunnel for target {:?}",
            attempt + 1,
            target_nodename
        );

        let (tunnel_client, tunnel_server) = fidl::endpoints::create_endpoints()?;

        match rc.reverse_tcp(&mut SocketAddressExt(tunnel_addr).into(), tunnel_client).await? {
            Ok(()) => {
                let tunnel_stream = tunnel_server.into_stream().with_context(|| {
                    format!("getting tunnel stream for target {:?}", target_nodename)
                })?;

                return Ok(tunnel_stream);
            }
            Err(rcs::TunnelError::ConnectFailed) => {
                log::warn!("failed to bind repository tunnel port on target {:?}", target_nodename);

                // Another process is using the port. Sleep and retry.
                fasync::Timer::new(TUNNEL_CONNECT_RETRY_TIMEOUT).await;
            }
            Err(err) => {
                return Err(anyhow::anyhow!(
                    "failed to create repository tunnel for target {:?}: {:?}",
                    target_nodename,
                    err
                ));
            }
        }
    }

    Err(anyhow::anyhow!("failed to bind to tunnel port on target {:?}", target_nodename))
}

async fn run_tunnel_protocol(
    target_nodename: &str,
    tunnel_stream: rcs::ForwardCallbackRequestStream,
    server_sink: UnboundedSender<Result<ConnectionStream>>,
) {
    let result = tunnel_stream
        .map(|request| match request {
            Ok(rcs::ForwardCallbackRequest::Forward { socket, addr, .. }) => {
                let addr = SocketAddressExt::from(addr);
                log::info!("tunneling connection from target {:?} to {}", target_nodename, addr);

                Ok(fasync::Socket::from_socket(socket)
                    .map(|socket| ConnectionStream::Socket(socket))
                    .map_err(Into::into))
            }
            Err(e) => Ok(Err(anyhow::Error::from(e))),
        })
        .forward(server_sink)
        .await;

    match result {
        Ok(()) => {
            log::info!("closed repository tunnel stream from target {:?}", target_nodename);
        }
        Err(err) => {
            log::error!("error forwarding tunnel from target {:?}: {}", target_nodename, err);
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::net::Ipv4Addr};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_spawning_tunnel() {
        let tunnel_addr = (Ipv4Addr::LOCALHOST, 8085).into();
        let (server_tx, _server_rx) = futures::channel::mpsc::unbounded();
        let _tunnel_manager = TunnelManager::new(tunnel_addr, server_tx);
    }
}
