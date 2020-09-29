// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod serial;

use crate::serial::run_serial_link_handlers;
use anyhow::{bail, ensure, format_err, Error};
use argh::FromArgs;
use fidl_fuchsia_overnet_protocol::StreamSocketGreeting;
use futures::prelude::*;
use overnet_core::{
    new_deframer, new_framer, DeframerReader, DeframerWriter, FrameType, FramerReader,
    FramerWriter, LosslessBinary, Router, RouterOptions,
};
use std::sync::Arc;

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

async fn read_incoming(
    mut stream: &async_std::os::unix::net::UnixStream,
    mut incoming_writer: DeframerWriter<LosslessBinary>,
) -> Result<(), Error> {
    let mut buf = [0u8; 16384];

    loop {
        let n = stream.read(&mut buf).await?;
        if n == 0 {
            return Err(format_err!("Incoming socket closed"));
        }
        incoming_writer.write(&buf[..n]).await?;
    }
}

async fn write_outgoing(
    mut outgoing_reader: FramerReader<LosslessBinary>,
    mut stream: &async_std::os::unix::net::UnixStream,
) -> Result<(), Error> {
    loop {
        let out = outgoing_reader.read().await?;
        stream.write_all(&out).await?;
    }
}

async fn process_incoming(
    node: Arc<Router>,
    mut rx_frames: DeframerReader<LosslessBinary>,
    mut tx_frames: FramerWriter<LosslessBinary>,
    sockpath: &str,
) -> Result<(), Error> {
    let node_id = node.node_id();

    // Send first frame
    let mut greeting = StreamSocketGreeting {
        magic_string: Some(hoist::ASCENDD_SERVER_CONNECTION_STRING.to_string()),
        node_id: Some(node_id.into()),
        connection_label: Some(format!(
            "ascendd via {:?} pid:{}",
            std::env::current_exe(),
            std::process::id()
        )),
        key: None,
    };
    let mut bytes = Vec::new();
    let mut handles = Vec::new();
    fidl::encoding::Encoder::encode(&mut bytes, &mut handles, &mut greeting)?;
    assert_eq!(handles.len(), 0);
    tx_frames.write(FrameType::Overnet, bytes.as_slice()).await?;

    let (frame_type, mut frame) = rx_frames.read().await?;
    ensure!(frame_type == Some(FrameType::Overnet), "Expect only overnet frames");

    let mut greeting = StreamSocketGreeting::empty();
    // WARNING: Since we are decoding without a transaction header, we have to
    // provide a context manually. This could cause problems in future FIDL wire
    // format migrations, which are driven by header flags.
    let context = fidl::encoding::Context {};
    fidl::encoding::Decoder::decode_with_context(&context, frame.as_mut(), &mut [], &mut greeting)?;

    log::info!("Ascendd gets greeting: {:?}", greeting);

    let node_id = match greeting {
        StreamSocketGreeting { magic_string: None, .. } => anyhow::bail!(
            "Required magic string '{}' not present in greeting",
            hoist::ASCENDD_CLIENT_CONNECTION_STRING
        ),
        StreamSocketGreeting { magic_string: Some(ref x), .. }
            if x != hoist::ASCENDD_CLIENT_CONNECTION_STRING =>
        {
            anyhow::bail!(
                "Expected magic string '{}' in greeting, got '{}'",
                hoist::ASCENDD_CLIENT_CONNECTION_STRING,
                x
            )
        }
        StreamSocketGreeting { node_id: None, .. } => anyhow::bail!("No node id in greeting"),
        StreamSocketGreeting { node_id: Some(n), .. } => n.id,
    };

    // Register our new link!
    let sockpath = sockpath.to_string();
    let (link_sender, link_receiver) = node
        .new_link(
            node_id.into(),
            Box::new(move || {
                Some(fidl_fuchsia_overnet_protocol::LinkConfig::AscenddServer(
                    fidl_fuchsia_overnet_protocol::AscenddLinkConfig {
                        path: Some(sockpath.clone()),
                        connection_label: None,
                    },
                ))
            }),
        )
        .await?;
    let _: ((), ()) = futures::future::try_join(
        async move {
            let mut buf = [0u8; 4096];
            while let Some(n) = link_sender.next_send(&mut buf).await? {
                tx_frames.write(FrameType::Overnet, &buf[..n]).await?;
            }
            Ok(())
        },
        async move {
            loop {
                let (frame_type, mut frame) = rx_frames.read().await?;
                ensure!(frame_type == Some(FrameType::Overnet), "Expect only overnet frames");
                if let Err(err) = link_receiver.received_packet(frame.as_mut()).await {
                    log::trace!("Failed handling packet: {:?}", err);
                }
            }
        },
    )
    .await?;
    Ok(())
}

async fn run_stream(
    node: Arc<Router>,
    stream: Result<async_std::os::unix::net::UnixStream, std::io::Error>,
    sockpath: &str,
) -> Result<(), Error> {
    let stream = stream?;
    let (framer, outgoing_reader) = new_framer(LosslessBinary, 4096);
    let (incoming_writer, deframer) = new_deframer(LosslessBinary);
    log::info!("Processing new Ascendd socket");
    futures::future::try_join3(
        read_incoming(&stream, incoming_writer),
        write_outgoing(outgoing_reader, &stream),
        process_incoming(node, deframer, framer, sockpath),
    )
    .await
    .map(drop)
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
