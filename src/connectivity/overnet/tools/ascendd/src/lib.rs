// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod serial;

use crate::serial::run_serial_link_handlers;
use anyhow::{bail, Error};
use argh::FromArgs;
use futures::prelude::*;
use overnet_core::{Router, RouterOptions};
use std::sync::Arc;
use stream_link::run_stream_link;

#[derive(FromArgs, Default)]
/// daemon to lift a non-Fuchsia device into Overnet.
pub struct Opt {
    #[argh(option, long = "sockpath")]
    /// path to the ascendd socket.
    /// If not provided, this will default to a new socket-file in /tmp.
    pub sockpath: Option<String>,

    #[argh(option, long = "serial")]
    /// selector for which serial devices to communicate over.
    /// Could be 'none' to not communcate over serial, 'all' to query all serial devices and try to
    /// communicate with them, or a path to a serial device to communicate over *that* device.
    /// If not provided, this will default to 'none'.
    pub serial: Option<String>,
}

async fn run_stream(
    node: Arc<Router>,
    stream: Result<async_std::os::unix::net::UnixStream, std::io::Error>,
    sockpath: &str,
) -> Result<(), Error> {
    let (mut rx, mut tx) = stream?.split();
    let sockpath = sockpath.to_string();
    let config = Box::new(move || {
        Some(fidl_fuchsia_overnet_protocol::LinkConfig::AscenddServer(
            fidl_fuchsia_overnet_protocol::AscenddLinkConfig {
                path: Some(sockpath.clone()),
                connection_label: None,
                ..fidl_fuchsia_overnet_protocol::AscenddLinkConfig::EMPTY
            },
        ))
    });
    log::trace!("Processing new Ascendd socket");
    run_stream_link(node, &mut rx, &mut tx, config).await
}

pub async fn run_ascendd(opt: Opt, stdout: impl AsyncWrite + Unpin + Send) -> Result<(), Error> {
    let Opt { sockpath, serial } = opt;

    let sockpath = &sockpath.unwrap_or(hoist::DEFAULT_ASCENDD_PATH.to_string());
    let serial = serial.unwrap_or("none".to_string());

    log::info!("starting ascendd on {}", sockpath);

    let incoming = loop {
        match async_std::os::unix::net::UnixListener::bind(sockpath).await {
            Ok(listener) => {
                break listener;
            }
            Err(_) => {
                if async_std::os::unix::net::UnixStream::connect(sockpath).await.is_ok() {
                    log::error!("another ascendd is already listening at {}", sockpath);
                    bail!("another ascendd is aleady listening!");
                } else {
                    log::info!("cleaning up stale ascendd socket at {}", sockpath);
                    std::fs::remove_file(sockpath)?;
                };
            }
        }
    };

    log::info!("ascendd listening to socket {}", sockpath);

    let node = Router::new(
        RouterOptions::new()
            .export_diagnostics(fidl_fuchsia_overnet_protocol::Implementation::Ascendd),
        Box::new(hoist::hard_coded_security_context()),
    )?;

    futures::future::try_join(
        run_serial_link_handlers(Arc::downgrade(&node), &serial, stdout),
        async move {
            incoming
                .incoming()
                .for_each_concurrent(None, |stream| {
                    let node = node.clone();
                    async move {
                        if let Err(e) = run_stream(node, stream, sockpath).await {
                            log::warn!("Failed processing socket: {:?}", e);
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
