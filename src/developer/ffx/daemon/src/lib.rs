// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::{DAEMON, MAX_RETRY_COUNT, SOCKET},
    crate::daemon::Daemon,
    anyhow::{Context, Error},
    fidl::endpoints::{ClientEnd, RequestStream, ServiceMarker},
    fidl_fuchsia_developer_bridge::{DaemonMarker, DaemonProxy, DaemonRequestStream},
    fidl_fuchsia_overnet::{
        ServiceConsumerProxyInterface, ServiceProviderRequest, ServiceProviderRequestStream,
    },
    fidl_fuchsia_overnet_protocol::NodeId,
    fuchsia_async::Task,
    futures::prelude::*,
    std::env,
    std::process::Command,
};

mod constants;
mod daemon;
mod discovery;
mod events;
mod mdns;
mod net;
mod onet;
mod ssh;
mod util;

pub mod target;

pub async fn create_daemon_proxy(id: &mut NodeId) -> Result<DaemonProxy, Error> {
    let svc = hoist::connect_as_service_consumer()?;
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    svc.connect_to_service(id, DaemonMarker::NAME, s)?;
    let proxy = fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
    Ok(DaemonProxy::new(proxy))
}

// Note that this function assumes the daemon has been started separately.
pub async fn find_and_connect() -> Result<Option<DaemonProxy>, Error> {
    let svc = hoist::connect_as_service_consumer()?;
    // Sometimes list_peers doesn't properly report the published services - retry a few times
    // but don't loop indefinitely.
    for _ in 0..MAX_RETRY_COUNT {
        let peers = svc.list_peers().await?;
        for mut peer in peers {
            if peer.description.services.is_none() {
                continue;
            }
            if peer
                .description
                .services
                .unwrap()
                .iter()
                .find(|name| *name == DaemonMarker::NAME)
                .is_none()
            {
                continue;
            }
            return create_daemon_proxy(&mut peer.id).map(|r| r.map(|proxy| Some(proxy))).await;
        }
    }

    Ok(None)
}

pub async fn spawn_daemon() -> Result<(), Error> {
    Command::new(env::current_exe().unwrap()).arg(DAEMON).arg("start").spawn()?;
    Ok(())
}
////////////////////////////////////////////////////////////////////////////////
// Overnet Server implementation

async fn next_request(
    stream: &mut ServiceProviderRequestStream,
) -> Result<Option<ServiceProviderRequest>, Error> {
    Ok(stream.try_next().await.context("error running service provider server")?)
}

async fn exec_server(daemon: Daemon) -> Result<(), Error> {
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
    let mut stream = ServiceProviderRequestStream::from_channel(chan);
    hoist::publish_service(DaemonMarker::NAME, ClientEnd::new(p))?;
    while let Some(ServiceProviderRequest::ConnectToService {
        chan,
        info: _,
        control_handle: _control_handle,
    }) = next_request(&mut stream).await?
    {
        log::trace!("Received service request for service");
        let chan =
            fidl::AsyncChannel::from_channel(chan).context("failed to make async channel")?;
        let daemon_clone = daemon.clone();
        Task::spawn(async move {
            daemon_clone
                .handle_requests_from_stream(DaemonRequestStream::from_channel(chan))
                .await
                .unwrap_or_else(|err| panic!("fatal error handling request: {:?}", err));
        })
        .detach();
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// start

pub fn is_daemon_running() -> bool {
    // Try to connect directly to the socket. This will fail if nothing is listening on the other side
    // (even if the path exists).
    match std::os::unix::net::UnixStream::connect(SOCKET) {
        Ok(_) => true,
        Err(_) => false,
    }
}

pub async fn start() -> Result<(), Error> {
    if is_daemon_running() {
        return Ok(());
    }
    futures::try_join!(onet::run_ascendd(), exec_server(Daemon::new().await?))?;
    Ok(())
}
