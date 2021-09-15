// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    errors::{ffx_error, FfxError},
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_bridge::{DaemonMarker, DaemonProxy},
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::prelude::*,
    hoist::OvernetInstance,
    libc,
    std::os::unix::process::CommandExt,
    std::pin::Pin,
    std::process::Command,
    std::time::Duration,
};

mod constants;
mod daemon;
mod target_control;

pub use constants::{get_socket, LOG_FILE_PREFIX};

pub use daemon::Daemon;

async fn create_daemon_proxy(id: &mut NodeId) -> Result<DaemonProxy> {
    let svc = hoist::hoist().connect_as_service_consumer()?;
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    svc.connect_to_service(id, DaemonMarker::NAME, s)?;
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

    let link = hoist::hoist().run_single_ascendd_link().fuse();
    let mut link = Box::pin(link);
    let find = find_next_daemon(exclusions).fuse();
    let mut find = Box::pin(find);
    let mut timeout = fuchsia_async::Timer::new(Duration::from_secs(5)).fuse();

    let res = futures::select! {
        r = link => {
            Err(ffx_error!("Daemon link lost while attempting to connect: {:?}\nRun `ffx doctor` for further diagnostics.", r))
        }
        _ = timeout => {
            Err(ffx_error!("Timed out waiting for the ffx daemon on the Overnet mesh.\nRun `ffx doctor --restart-daemon` for further diagnostics."))
        }
        proxy = find => proxy.map_err(|e| ffx_error!("Error connecting to Daemon: {}.\nRun `ffx doctor` for further diagnostics.", e)),
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
                .find(|name| *name == DaemonMarker::NAME)
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
    log::info!("Starting new ffx background daemon from {:?}", &ffx_path);

    let ffx: Ffx = argh::from_env();
    let mut stdout = Stdio::null();
    let mut stderr = Stdio::null();

    if ffx_config::logging::is_enabled().await {
        stdout = Stdio::from(ffx_config::logging::log_file(LOG_FILE_PREFIX).await?);
        stderr = Stdio::from(ffx_config::logging::log_file(LOG_FILE_PREFIX).await?);
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
            log::info!("no daemon found at {}", &path);
            return false;
        }
        Err(e) => {
            log::info!("error stating {}: {}", &path, e);
            // speculatively carry on
        }
    }

    match std::os::unix::net::UnixStream::connect(&path) {
        Ok(sock) => match sock.peer_addr() {
            Ok(_) => {
                log::info!("found running daemon at {}", &path);
                true
            }
            Err(err) => {
                log::info!("found daemon socket at {} but could not see peer: {}", &path, err);
                false
            }
        },
        Err(err) => {
            log::info!("failed to connect to daemon at {}: {}", &path, err);
            false
        }
    }
}

// daemonize adds a pre_exec to call daemon(3) causing the spawned
// process to be forked again and detached from the controlling
// terminal.
fn daemonize(c: &mut Command) -> &mut Command {
    unsafe {
        c.pre_exec(|| {
            // daemonize(3) is deprecated on macOS 10.15. The replacement is not
            // yet clear, we may want to replace this with a manual double fork
            // setsid, etc.
            #[allow(deprecated)]
            // First argument: chdir(/)
            // Second argument: do not close stdio (we use stdio to write to the daemon log file)
            match libc::daemon(0, 1) {
                0 => Ok(()),
                x => Err(std::io::Error::from_raw_os_error(x)),
            }
        })
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_daemonize() -> Result<()> {
        let started = std::time::Instant::now();
        // TODO(raggi): this technically leaks a sleep process, which is
        // not ideal, but the much better approach would be a
        // significant amount of work, as we'd really want a program
        // that will wait for a signal on some other channel (such as a
        // unix socket) and otherwise linger as a daemon. If we had
        // that, we could then check the ppid and assert that daemon(3)
        // really did the work we're expecting it to. As that would
        // involve specific subprograms, finding those, and so on, it is
        // likely beyond ROI for this test coverage, which aims to just
        // prove that the immediate spawn() succeeded was detached from
        // the program in question. There is a risk that this
        // implementation passes if sleep(1) is not found, which is also
        // not ideal.
        let mut child = daemonize(Command::new("sleep").arg("10")).spawn()?;
        child.wait()?;
        assert!(started.elapsed() < std::time::Duration::from_secs(10));
        Ok(())
    }
}
