// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    daemonize::daemonize,
    errors::{ffx_error, FfxError},
    ffx_config::EnvironmentContext,
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl_fuchsia_developer_ffx::{DaemonMarker, DaemonProxy},
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::prelude::*,
    hoist::{Hoist, OvernetInstance},
    std::path::{Path, PathBuf},
    std::pin::Pin,
    std::time::Duration,
};

mod constants;
mod daemon;
mod socket;

pub use constants::LOG_FILE_PREFIX;

pub use daemon::Daemon;

pub use socket::SocketDetails;

async fn create_daemon_proxy(hoist: &Hoist, id: &mut NodeId) -> Result<DaemonProxy> {
    let svc = hoist.connect_as_service_consumer()?;
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    svc.connect_to_service(id, DaemonMarker::PROTOCOL_NAME, s)?;
    let proxy = fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
    Ok(DaemonProxy::new(proxy))
}

pub async fn get_daemon_proxy_single_link(
    hoist: &Hoist,
    socket_path: PathBuf,
    exclusions: Option<Vec<NodeId>>,
) -> Result<(NodeId, DaemonProxy, Pin<Box<impl Future<Output = Result<()>>>>), FfxError> {
    // Start a race betwen:
    // - The unix socket link being lost
    // - A timeout
    // - Getting a FIDL proxy over the link

    let link = hoist.clone().run_single_ascendd_link(socket_path.clone()).fuse();
    let mut link = Box::pin(link);
    let find = find_next_daemon(hoist, exclusions).fuse();
    let mut find = Box::pin(find);
    let mut timeout = fuchsia_async::Timer::new(Duration::from_secs(5)).fuse();

    let res = futures::select! {
        r = link => {
            Err(ffx_error!("Daemon link lost while attempting to connect to socket {}: {:#?}\nRun `ffx doctor` for further diagnostics.", socket_path.display(), r))
        }
        _ = timeout => {
            Err(ffx_error!("Timed out waiting for the ffx daemon on the Overnet mesh over socket {}.\nRun `ffx doctor --restart-daemon` for further diagnostics.", socket_path.display()))
        }
        proxy = find => proxy.map_err(|e| ffx_error!("Error connecting to Daemon at socket: {}: {:#?}\nRun `ffx doctor` for further diagnostics.", socket_path.display(), e)),
    };
    res.map(|(nodeid, proxy)| (nodeid, proxy, link))
}

async fn find_next_daemon<'a>(
    hoist: &Hoist,
    exclusions: Option<Vec<NodeId>>,
) -> Result<(NodeId, DaemonProxy)> {
    let svc = hoist.connect_as_service_consumer()?;
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
            return create_daemon_proxy(hoist, &mut peer.id.clone())
                .await
                .map(|proxy| (peer.id.clone(), proxy));
        }
    }
}

// Note that this function assumes the daemon has been started separately.
pub async fn find_and_connect(hoist: &Hoist, socket_path: PathBuf) -> Result<DaemonProxy> {
    // This function is due for deprecation/removal. It should only be used
    // currently by the doctor daemon_manager, which should instead learn to
    // understand the link state in future revisions.
    get_daemon_proxy_single_link(hoist, socket_path, None)
        .await
        .map(|(_nodeid, proxy, link_fut)| {
            fuchsia_async::Task::local(link_fut.map(|_| ())).detach();
            proxy
        })
        .context("connecting to the ffx daemon")
}

pub async fn spawn_daemon(context: &EnvironmentContext) -> Result<()> {
    use std::process::Stdio;

    let mut cmd = context.rerun_prefix()?;
    let socket_path = context
        .load()
        .await
        .context("Loading environment")?
        .get_ascendd_path()
        .context("No socket path configured")?;
    tracing::info!("Starting new ffx background daemon from {:?}", &cmd.get_program());

    let mut stdout = Stdio::null();
    let mut stderr = Stdio::null();

    if ffx_config::logging::is_enabled().await {
        stdout = Stdio::from(ffx_config::logging::log_file(LOG_FILE_PREFIX, true).await?);
        // Second argument is false, meaning don't perform log rotation. We rotated the logs once
        // for the call above, we shouldn't do it again.
        stderr = Stdio::from(ffx_config::logging::log_file(LOG_FILE_PREFIX, false).await?);
    }

    cmd.stdin(Stdio::null()).stdout(stdout).stderr(stderr).env("RUST_BACKTRACE", "full");
    cmd.arg("daemon");
    cmd.arg("start");
    cmd.arg("--path").arg(socket_path);
    daemonize(&mut cmd)
        .spawn()
        .context("spawning daemon start")?
        .wait()
        .map(|_| ())
        .context("waiting for daemon start")
}

////////////////////////////////////////////////////////////////////////////////
// start

pub fn is_daemon_running_at_path(socket_path: &Path) -> bool {
    // Not strictly necessary check, but improves log output for diagnostics
    match std::fs::metadata(socket_path) {
        Ok(_) => {}
        Err(ref e) if e.kind() == std::io::ErrorKind::NotFound => {
            tracing::info!("no daemon found at {}", socket_path.display());
            return false;
        }
        Err(e) => {
            tracing::info!("error stating {}: {}", socket_path.display(), e);
            // speculatively carry on
        }
    }

    let sock = hoist::short_socket_path(&socket_path)
        .and_then(|safe_socket_path| std::os::unix::net::UnixStream::connect(&safe_socket_path));
    match sock {
        Ok(sock) => match sock.peer_addr() {
            Ok(_) => {
                tracing::info!("found running daemon at {}", socket_path.display());
                true
            }
            Err(err) => {
                tracing::info!(
                    "found daemon socket at {} but could not see peer: {}",
                    socket_path.display(),
                    err
                );
                false
            }
        },
        Err(err) => {
            tracing::info!("failed to connect to daemon at {}: {}", socket_path.display(), err);
            false
        }
    }
}
