// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::{get_socket, DAEMON, DEFAULT_MAX_RETRY_COUNT, OVERNET_MAX_RETRY_COUNT},
    crate::daemon::Daemon,
    anyhow::{bail, Context, Result},
    ffx_config::get,
    ffx_lib_args::Ffx,
    fidl::endpoints::{ClientEnd, RequestStream, ServiceMarker},
    fidl_fuchsia_developer_bridge::{DaemonMarker, DaemonProxy, DaemonRequestStream},
    fidl_fuchsia_overnet::{
        ServiceConsumerProxyInterface, ServiceProviderRequest, ServiceProviderRequestStream,
    },
    fidl_fuchsia_overnet_protocol::NodeId,
    fuchsia_async::Task,
    futures::prelude::*,
    libc,
    std::env,
    std::os::unix::process::CommandExt,
    std::process::{Command, Stdio},
};

mod constants;
mod daemon;
mod discovery;
mod events;
mod fastboot;
mod mdns;
mod net;
mod onet;
mod ssh;
mod target_task;
mod task;
mod util;

pub mod target;

pub async fn create_daemon_proxy(id: &mut NodeId) -> Result<DaemonProxy> {
    let svc = hoist::connect_as_service_consumer()?;
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    svc.connect_to_service(id, DaemonMarker::NAME, s)?;
    let proxy = fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
    Ok(DaemonProxy::new(proxy))
}

// Note that this function assumes the daemon has been started separately.
pub async fn find_and_connect() -> Result<DaemonProxy> {
    let svc = hoist::connect_as_service_consumer()?;
    // Sometimes list_peers doesn't properly report the published services - retry a few times
    // but don't loop indefinitely.
    let max_retry_count: u64 =
        get(OVERNET_MAX_RETRY_COUNT).await.unwrap_or(DEFAULT_MAX_RETRY_COUNT);
    for _ in 0..max_retry_count {
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
            return create_daemon_proxy(&mut peer.id).await;
        }
    }

    bail!("Timed out waiting for ffx daemon connection")
}

pub async fn spawn_daemon() -> Result<()> {
    let mut ffx_path = env::current_exe()?;
    // when we daemonize, our path will change to /, so get the canonical path before that occurs.
    ffx_path = std::fs::canonicalize(ffx_path)?;
    log::info!("Starting new ffx background daemon from {:?}", &ffx_path);

    let ffx: Ffx = argh::from_env();
    let mut stdout = Stdio::null();
    let mut stderr = Stdio::null();

    if ffx.verbose {
        stdout = Stdio::inherit();
        stderr = Stdio::inherit();
    } else {
        if ffx_config::logging::is_enabled().await {
            // TODO(raggi): maybe dup instead.
            stdout = Stdio::from(ffx_config::logging::log_file("ffx.daemon").await?);
            stderr = Stdio::from(ffx_config::logging::log_file("ffx.daemon").await?);
        }
    }

    let mut cmd = Command::new(ffx_path);
    cmd.stdin(Stdio::null())
        .stdout(stdout)
        .stderr(stderr)
        .env("RUST_BACKTRACE", "full")
        .arg(DAEMON)
        .arg("start");
    if let Some(c) = ffx.config.as_ref() {
        cmd.arg("--config").arg(c);
    }
    if let Some(e) = ffx.env.as_ref() {
        cmd.arg("--env").arg(e);
    }
    daemonize(&mut cmd)
        .spawn()
        .context("spawning daemon start")?
        .wait()
        .map(|_| ())
        .context("waiting for daemon start")
}

////////////////////////////////////////////////////////////////////////////////
// Overnet Server implementation

async fn next_request(
    stream: &mut ServiceProviderRequestStream,
) -> Result<Option<ServiceProviderRequest>> {
    Ok(stream.try_next().await.context("error running service provider server")?)
}
async fn exec_server(daemon: Daemon) -> Result<()> {
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

pub async fn is_daemon_running() -> bool {
    // Try to connect directly to the socket. This will fail if nothing is listening on the other side
    // (even if the path exists).
    let path = get_socket().await;
    let sock = match std::os::unix::net::UnixDatagram::unbound() {
        Ok(s) => s,
        Err(_) => return false,
    };
    match sock.connect(path) {
        Ok(_) => sock.peer_addr().is_ok(),
        Err(_) => false,
    }
}

pub async fn start() -> Result<()> {
    future::try_join(onet::run_ascendd(), exec_server(Daemon::new().await?)).await?;
    Ok(())
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
