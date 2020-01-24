// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Controls one link to another node over a zx socket.

use {
    crate::{
        coding::{decode_fidl, encode_fidl},
        future_help::log_errors,
        labels::NodeId,
        router::Router,
        runtime::{spawn, wait_until},
        stream_framer::{StreamDeframer, StreamFramer},
    },
    anyhow::{Context as _, Error},
    fidl_fuchsia_overnet_protocol::StreamSocketGreeting,
    futures::{io::AsyncWriteExt, prelude::*},
    std::{
        rc::Rc,
        time::{Duration, Instant},
    },
};

async fn read_or_unstick(
    buf: &mut [u8],
    deframer: &mut StreamDeframer,
    rx_bytes: &mut futures::io::ReadHalf<fidl::AsyncSocket>,
    duration_per_byte: Option<Duration>,
) -> Result<Option<usize>, Error> {
    if let Some(duration_per_byte) = duration_per_byte {
        if deframer.is_stuck() {
            let mut rx_read_bytes = rx_bytes.read(buf).fuse();
            return futures::select! {
                r = rx_read_bytes => r.map_err(|e| e.into()).map(Some),
                _ = wait_until(Instant::now() + duration_per_byte).fuse() => {
                    deframer.skip_byte();
                    Ok(None)
                }
            };
        }
    }
    rx_bytes.read(buf).await.map_err(|e| e.into()).map(Some)
}

async fn socket_deframer(
    mut rx_bytes: futures::io::ReadHalf<fidl::AsyncSocket>,
    mut tx_frames: futures::channel::mpsc::Sender<Vec<u8>>,
    duration_per_byte: Option<Duration>,
) -> Result<(), Error> {
    let mut deframer = StreamDeframer::new();
    let mut buf = [0u8; 1024];

    loop {
        if let Some(n) =
            read_or_unstick(&mut buf, &mut deframer, &mut rx_bytes, duration_per_byte).await?
        {
            deframer.queue_recv(&buf[..n]);
        }
        while let Some(frame) = deframer.next_incoming_frame() {
            tx_frames.send(frame).await?;
        }
    }
}

pub(crate) fn spawn_socket_link(
    node: Rc<Router>,
    node_id: NodeId,
    connection_label: Option<String>,
    socket: fidl::Socket,
    duration_per_byte: Option<Duration>,
) {
    spawn(log_errors(
        run_socket_link(node, node_id, connection_label, socket, duration_per_byte),
        "Socket link failed",
    ));
}

async fn run_socket_link(
    node: Rc<Router>,
    node_id: NodeId,
    connection_label: Option<String>,
    socket: fidl::Socket,
    duration_per_byte: Option<Duration>,
) -> Result<(), Error> {
    log::info!("Begin handshake: connection_label:{:?} socket:{:?}", connection_label, socket);

    const GREETING_STRING: &str = "OVERNET SOCKET LINK";

    // Send first frame
    let mut framer = StreamFramer::new();
    let mut greeting = StreamSocketGreeting {
        magic_string: Some(GREETING_STRING.to_string()),
        node_id: Some(node_id.into()),
        connection_label,
    };
    framer
        .queue_send(encode_fidl(&mut greeting).context("encoding greeting")?.as_slice())
        .context("queue greeting")?;
    let send = framer.take_sends();
    assert_eq!(send.len(), socket.write(&send).context("write greeting")?);

    // Wait for first frame
    let (rx_bytes, mut tx_bytes) = futures::io::AsyncReadExt::split(
        fidl::AsyncSocket::from_socket(socket).context("asyncify socket")?,
    );
    let (tx_frames, mut rx_frames) = futures::channel::mpsc::channel(1);
    spawn(log_errors(
        socket_deframer(rx_bytes, tx_frames, duration_per_byte),
        "Error reading/deframing socket",
    ));
    let mut greeting_bytes =
        rx_frames.next().await.ok_or(anyhow::format_err!("No greeting received on socket"))?;
    let greeting = decode_fidl::<StreamSocketGreeting>(greeting_bytes.as_mut())
        .context("decoding greeting")?;
    let node_id = match greeting {
        StreamSocketGreeting { magic_string: None, .. } => {
            return Err(anyhow::format_err!(
                "Required magic string '{}' not present in greeting",
                GREETING_STRING
            ))
        }
        StreamSocketGreeting { magic_string: Some(ref x), .. } if x != GREETING_STRING => {
            return Err(anyhow::format_err!(
                "Expected magic string '{}' in greeting, got '{}'",
                GREETING_STRING,
                x
            ))
        }
        StreamSocketGreeting { node_id: None, .. } => {
            return Err(anyhow::format_err!("No node id in greeting"))
        }
        StreamSocketGreeting { node_id: Some(n), .. } => n.id,
    };

    log::trace!("Handshake complete, creating link");
    let link_sender = node.new_link(node_id.into()).await.context("creating link")?;
    let link_receiver = link_sender.clone();

    log::trace!("Running link");
    spawn(async move {
        while let Some(mut frame) = rx_frames.next().await {
            //log::trace!("RECV LINK FRAME: {:?}", &frame);
            if let Err(err) = link_receiver.received_packet(frame.as_mut()).await {
                log::warn!("Error reading packet: {:?}", err);
            }
        }
    });

    let mut buf = [0u8; 4096];
    while let Some(n) = link_sender.next_send(&mut buf).await? {
        // log::trace!("SEND LINK FRAME: {:?}", &buf[..n]);
        framer.queue_send(&buf[..n])?;
        tx_bytes.write_all(framer.take_sends().as_slice()).await?;
    }

    Ok(())
}
