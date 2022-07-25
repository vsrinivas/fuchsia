// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    daemonize::daemonize,
    errors::{ffx_error, FfxError},
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl_fuchsia_developer_ffx::{DaemonMarker, DaemonProxy},
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::prelude::*,
    hoist::OvernetInstance,
    std::pin::Pin,
    std::process::Command,
    std::time::Duration,
};

mod constants;
mod daemon;

pub use constants::{get_socket, LOG_FILE_PREFIX};

pub use daemon::Daemon;

async fn create_daemon_proxy(id: &mut NodeId) -> Result<DaemonProxy> {
    let svc = hoist::hoist().connect_as_service_consumer()?;
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    svc.connect_to_service(id, DaemonMarker::PROTOCOL_NAME, s)?;
    let proxy = fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
    Ok(DaemonProxy::new(proxy))
}

pub async fn get_daemon_proxy_single_link(
    exclusions: Option<Vec<NodeId>>,
) -> Result<(NodeId, DaemonProxy, Pin<Box<impl Future<Output = Result<()>>>>), FfxError> {
    // Start a race betwen:
    // - The unix socket link being lost
    // - A timeout
    // - Getting a FIDL proxy over the link

    let ascendd_socket_path = constants::get_socket().await;
    let link = hoist::hoist().run_single_ascendd_link(ascendd_socket_path.clone()).fuse();
    let mut link = Box::pin(link);
    let find = find_next_daemon(exclusions).fuse();
    let mut find = Box::pin(find);
    let mut timeout = fuchsia_async::Timer::new(Duration::from_secs(5)).fuse();

    let res = futures::select! {
        r = link => {
            Err(ffx_error!("Daemon link lost while attempting to connect to socket {}: {:#?}\nRun `ffx doctor` for further diagnostics.", ascendd_socket_path, r))
        }
        _ = timeout => {
            Err(ffx_error!("Timed out waiting for the ffx daemon on the Overnet mesh over socket {}.\nRun `ffx doctor --restart-daemon` for further diagnostics.", ascendd_socket_path))
        }
        proxy = find => proxy.map_err(|e| ffx_error!("Error connecting to Daemon at socket: {}: {:#?}\nRun `ffx doctor` for further diagnostics.", ascendd_socket_path, e)),
    };
    res.map(|(nodeid, proxy)| (nodeid, proxy, link))
}

async fn find_next_daemon<'a>(exclusions: Option<Vec<NodeId>>) -> Result<(NodeId, DaemonProxy)> {
    let svc = hoist::hoist().connect_as_service_consumer()?;
    loop {
        let peers = svc.list_peers().await?;
        for peer in peers.iter() {
            if peer
                .description
                .services
                .as_ref()
                .unwrap_or(&Vec::new())
                .iter()
                .find(|name| *name == DaemonMarker::PROTOCOL_NAME)
                .is_none()
            {
                continue;
            }
            match exclusions {
                Some(ref exclusions) => {
                    if exclusions.iter().any(|n| *n == peer.id) {
                        continue;
                    }
                }
                None => {}
            }
            return create_daemon_proxy(&mut peer.id.clone())
                .await
                .map(|proxy| (peer.id.clone(), proxy));
        }
    }
}

// Note that this function assumes the daemon has been started separately.
pub async fn find_and_connect() -> Result<DaemonProxy> {
    // This function is due for deprecation/removal. It should only be used
    // currently by the doctor daemon_manager, which should instead learn to
    // understand the link state in future revisions.
    get_daemon_proxy_single_link(None)
        .await
        .map(|(_nodeid, proxy, link_fut)| {
            fuchsia_async::Task::local(link_fut.map(|_| ())).detach();
            proxy
        })
        .context("connecting to the ffx daemon")
}

pub async fn spawn_daemon() -> Result<()> {
    use ffx_lib_args::Ffx;
    use std::env;
    use std::process::Stdio;

    let mut ffx_path = env::current_exe()?;
    // when we daemonize, our path will change to /, so get the canonical path before that occurs.
    ffx_path = std::fs::canonicalize(ffx_path)?;
    tracing::info!("Starting new ffx background daemon from {:?}", &ffx_path);

    let ffx: Ffx = argh::from_env();
    let mut stdout = Stdio::null();
    let mut stderr = Stdio::null();

    if ffx_config::logging::is_enabled().await {
        stdout = Stdio::from(ffx_config::logging::log_file(LOG_FILE_PREFIX, true).await?);
        // Second argument is false, meaning don't perform log rotation. We rotated the logs once
        // for the call above, we shouldn't do it again.
        stderr = Stdio::from(ffx_config::logging::log_file(LOG_FILE_PREFIX, false).await?);
    }

    let mut cmd = Command::new(ffx_path);
    cmd.stdin(Stdio::null()).stdout(stdout).stderr(stderr).env("RUST_BACKTRACE", "full");
    for c in ffx.config.iter() {
        cmd.arg("--config").arg(c);
    }
    if let Some(e) = ffx.env.as_ref() {
        cmd.arg("--env").arg(e);
    }
    cmd.arg("daemon");
    cmd.arg("start");
    daemonize(&mut cmd)
        .spawn()
        .context("spawning daemon start")?
        .wait()
        .map(|_| ())
        .context("waiting for daemon start")
}

////////////////////////////////////////////////////////////////////////////////
// start

pub async fn is_daemon_running() -> bool {
    // Try to connect directly to the socket. This will fail if nothing is listening on the other side
    // (even if the path exists).
    let path = get_socket().await;

    is_daemon_running_at_path(path)
}

pub fn is_daemon_running_at_path(path: String) -> bool {
    // Not strictly necessary check, but improves log output for diagnostics
    match std::fs::metadata(&path) {
        Ok(_) => {}
        Err(ref e) if e.kind() == std::io::ErrorKind::NotFound => {
            tracing::info!("no daemon found at {}", &path);
            return false;
        }
        Err(e) => {
            tracing::info!("error stating {}: {}", &path, e);
            // speculatively carry on
        }
    }

    match std::os::unix::net::UnixStream::connect(&path) {
        Ok(sock) => match sock.peer_addr() {
            Ok(_) => {
                tracing::info!("found running daemon at {}", &path);
                true
            }
            Err(err) => {
                tracing::info!("found daemon socket at {} but could not see peer: {}", &path, err);
                false
            }
        },
        Err(err) => {
            tracing::info!("failed to connect to daemon at {}: {}", &path, err);
            false
        }
    }
}
