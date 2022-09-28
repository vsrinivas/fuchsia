// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod serial;

use crate::serial::run_serial_link_handlers;
use anyhow::Context as ErrorContext;
use anyhow::{bail, format_err, Error};
use argh::FromArgs;
use async_net::unix::{UnixListener, UnixStream};
use fuchsia_async::Task;
use fuchsia_async::TimeoutExt;
use futures::prelude::*;
use hoist::Hoist;
use overnet_core::AscenddClientRouting;
use std::io::{
    ErrorKind::{self, TimedOut},
    Write,
};
use std::path::{Path, PathBuf};
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll};
use std::time::Duration;
use stream_link::run_stream_link;

// Limits the maximum concurrent ascendd tasks as a stop-gap solution around issues that have
// cropped up as a result of parallelized invocations into ffx (primarily for testing).
// See http://fxbug.dev/87372
const ASCENDD_MAX_CONCURRENT_TASKS: usize = 5;

#[cfg(not(target_os = "fuchsia"))]
pub use hoist::default_ascendd_path;

#[derive(FromArgs, Default)]
/// daemon to lift a non-Fuchsia device into Overnet.
pub struct Opt {
    #[argh(option, long = "sockpath")]
    /// path to the ascendd socket.
    /// If not provided, this will default to a new socket-file in /tmp.
    pub sockpath: Option<PathBuf>,

    #[argh(option, long = "serial")]
    /// selector for which serial devices to communicate over.
    /// Could be 'none' to not communcate over serial, 'all' to query all serial devices and try to
    /// communicate with them, or a path to a serial device to communicate over *that* device.
    /// If not provided, this will default to 'none'.
    pub serial: Option<String>,

    #[argh(option, long = "client-routing", default = "true")]
    /// route Ascendd clients to each other. Can be turned off to avoid scaling issues
    /// when multiple ffxs are run concurrently, with no requirement to have
    /// them interact. (Normally set to false iff run as ffx daemon.)
    pub client_routing: bool,
}

#[derive(Debug)]
pub struct Ascendd {
    task: Task<Result<(), Error>>,
}

impl Ascendd {
    pub async fn new(
        opt: Opt,
        hoist: &Hoist,
        stdout: impl AsyncWrite + Unpin + Send + 'static,
    ) -> Result<Self, Error> {
        let (sockpath, serial, client_routing, incoming) = bind_listener(opt, hoist).await?;
        Ok(Self {
            task: Task::spawn(run_ascendd(
                hoist.clone(),
                sockpath,
                serial,
                incoming,
                client_routing,
                stdout,
            )),
        })
    }
}

impl Future for Ascendd {
    type Output = Result<(), Error>;

    fn poll(mut self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Result<(), Error>> {
        self.task.poll_unpin(ctx)
    }
}

/// Run an ascendd server on the given stream IOs identified by the given labels
/// and paths, to completion.
pub fn run_stream<'a>(
    node: Arc<overnet_core::Router>,
    rx: &'a mut (dyn AsyncRead + Unpin + Send),
    tx: &'a mut (dyn AsyncWrite + Unpin + Send),
    label: Option<String>,
    path: Option<String>,
) -> impl Future<Output = Result<(), Error>> + 'a {
    let config = Box::new(move || {
        Some(fidl_fuchsia_overnet_protocol::LinkConfig::AscenddServer(
            fidl_fuchsia_overnet_protocol::AscenddLinkConfig {
                path: path.clone(),
                connection_label: label.clone(),
                ..fidl_fuchsia_overnet_protocol::AscenddLinkConfig::EMPTY
            },
        ))
    });

    run_stream_link(node, rx, tx, Default::default(), config)
}

async fn bind_listener(
    opt: Opt,
    hoist: &Hoist,
) -> Result<(PathBuf, String, AscenddClientRouting, UnixListener), Error> {
    let Opt { sockpath, serial, client_routing } = opt;
    let sockpath = sockpath.unwrap_or(default_ascendd_path());
    let serial = serial.unwrap_or("none".to_string());

    let client_routing =
        if client_routing { AscenddClientRouting::Enabled } else { AscenddClientRouting::Disabled };
    tracing::info!(
        node_id = hoist.node().node_id().0,
        "starting ascendd on {}",
        sockpath.display(),
    );

    let incoming = loop {
        let safe_socket_path = hoist::short_socket_path(&sockpath)?;
        match UnixListener::bind(&safe_socket_path) {
            Ok(listener) => {
                break listener;
            }
            Err(_) => match UnixStream::connect(&safe_socket_path)
                .on_timeout(Duration::from_secs(1), || {
                    Err(std::io::Error::new(TimedOut, format_err!("connecting to ascendd socket")))
                })
                .await
            {
                Ok(_) => {
                    tracing::error!(
                        "another ascendd is already listening at {}",
                        sockpath.display()
                    );
                    bail!("another ascendd is aleady listening at {}!", sockpath.display());
                }
                Err(e) if e.kind() == ErrorKind::ConnectionRefused => {
                    tracing::info!(
                        "trying to clean up stale ascendd socket at {} (error: {e:?})",
                        sockpath.display()
                    );
                    std::fs::remove_file(&sockpath)?;
                }
                Err(e) => {
                    tracing::info!("An unexpected error occurred while trying to bind to the ascendd socket at {}: {e:?}", sockpath.display());
                    bail!(
                        "unexpected error while trying to bind to ascendd socket at {}: {e}",
                        sockpath.display()
                    );
                }
            },
        }
    };

    // as this file is purely advisory, we won't fail for any error, but we can log it.
    if let Err(e) = write_pidfile(&sockpath, std::process::id()) {
        tracing::warn!("failed to write pidfile alongside {}: {e:?}", sockpath.display());
    }
    Ok((sockpath, serial, client_routing, incoming))
}

/// Writes a pid file alongside the socketpath so we know what pid last successfully tried to
/// create a socket there.
fn write_pidfile(sockpath: &Path, pid: u32) -> anyhow::Result<()> {
    let in_dir = sockpath.parent().context("No parent directory for socket path")?;
    let mut pidfile = tempfile::NamedTempFile::new_in(in_dir)?;
    write!(pidfile, "{pid}")?;
    pidfile.persist(sockpath.with_extension("pid"))?;
    Ok(())
}

async fn run_ascendd(
    hoist: Hoist,
    sockpath: PathBuf,
    serial: String,
    incoming: UnixListener,
    client_routing: AscenddClientRouting,
    stdout: impl AsyncWrite + Unpin + Send,
) -> Result<(), Error> {
    let node = hoist.node();
    node.set_implementation(fidl_fuchsia_overnet_protocol::Implementation::Ascendd);
    node.set_client_routing(client_routing);

    tracing::info!("ascendd listening to socket {}", sockpath.display());

    let sockpath = &sockpath.to_str().context("Non-unicode in socket path")?.to_owned();
    let hoist = &hoist;

    futures::future::try_join(
        run_serial_link_handlers(Arc::downgrade(&hoist.node()), &serial, stdout),
        async move {
            incoming
                .incoming()
                .for_each_concurrent(Some(ASCENDD_MAX_CONCURRENT_TASKS), |stream| async move {
                    match stream {
                        Ok(stream) => {
                            let (mut rx, mut tx) = stream.split();
                            if let Err(e) = run_stream(
                                hoist.node(),
                                &mut rx,
                                &mut tx,
                                None,
                                Some(sockpath.clone()),
                            )
                            .await
                            {
                                tracing::warn!("Failed serving socket: {:?}", e);
                            }
                        }
                        Err(e) => {
                            tracing::warn!("Failed starting socket: {:?}", e);
                        }
                    }
                })
                .await;
            Ok(())
        },
    )
    .await
    .map(drop)
}
