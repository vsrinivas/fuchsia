// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Controls one link to another node over a zx socket.

use {
    crate::{
        coding::{decode_fidl, encode_fidl},
        labels::NodeId,
        router::Router,
        stream_framer::{new_deframer, new_framer, Format, FrameType, LosslessBinary, LossyBinary},
    },
    anyhow::{bail, Context as _, Error},
    fidl_fuchsia_overnet_protocol::StreamSocketGreeting,
    futures::{io::AsyncWriteExt, prelude::*},
    std::{sync::Arc, time::Duration},
};

pub(crate) async fn run_socket_link(
    node: Arc<Router>,
    node_id: NodeId,
    connection_label: Option<String>,
    socket: fidl::Socket,
    duration_per_byte: Option<Duration>,
) -> Result<(), Error> {
    let handshake_id = crate::router::generate_node_id().0;
    log::info!(
        "[HS:{}] Begin handshake: connection_label:{:?} socket:{:?}",
        handshake_id,
        connection_label,
        socket
    );

    const GREETING_STRING: &str = "OVERNET SOCKET LINK";

    // Send first frame
    let (mut rx_bytes, mut tx_bytes) = futures::io::AsyncReadExt::split(
        fidl::AsyncSocket::from_socket(socket).context("asyncify socket")?,
    );
    let make_format = || -> Box<dyn Format> {
        if let Some(d) = duration_per_byte {
            Box::new(LossyBinary::new(d))
        } else {
            Box::new(LosslessBinary)
        }
    };
    let (mut framer, mut framer_read) = new_framer(make_format(), 4096);
    let (mut deframer_write, mut deframer) = new_deframer(make_format());
    let _: ((), (), ()) = futures::future::try_join3(
        async move {
            loop {
                let msg = framer_read.read().await?;
                tx_bytes.write(&msg).await?;
            }
        },
        async move {
            let mut buf = [0u8; 4096];
            loop {
                let n = rx_bytes.read(&mut buf).await?;
                if n == 0 {
                    return Ok(());
                }
                deframer_write.write(&buf[..n]).await?;
            }
        },
        async move {
            let mut greeting = StreamSocketGreeting {
                magic_string: Some(GREETING_STRING.to_string()),
                node_id: Some(node_id.into()),
                connection_label,
            };
            framer
                .write(
                    FrameType::Overnet,
                    encode_fidl(&mut greeting).context("encoding greeting")?.as_slice(),
                )
                .await
                .context("queue greeting")?;
            log::info!("[HS:{}] Wrote greeting: {:?}", handshake_id, greeting);
            // Wait for first frame
            let (frame_type, mut greeting_bytes) = deframer.read().await?;
            if frame_type != Some(FrameType::Overnet) {
                bail!("Expected Overnet frame, got {:?}", frame_type);
            }
            let greeting = decode_fidl::<StreamSocketGreeting>(greeting_bytes.as_mut())
                .context("decoding greeting")?;
            log::info!("[HS:{}] Got greeting: {:?}", handshake_id, greeting);
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
            log::info!("[HS:{}] Handshake complete, creating link", handshake_id);
            let link_sender = node.new_link(node_id.into()).await.context("creating link")?;
            let link_receiver = link_sender.clone();
            log::info!("[HS:{}] Running link", handshake_id);
            let _: ((), ()) = futures::future::try_join(
                async move {
                    loop {
                        let (frame_type, mut frame) = deframer.read().await?;
                        if frame_type != Some(FrameType::Overnet) {
                            continue;
                        }
                        if let Err(err) = link_receiver.received_packet(frame.as_mut()).await {
                            log::warn!("[HS:{}] Error reading packet: {:?}", handshake_id, err);
                        }
                    }
                },
                async move {
                    let mut buf = [0u8; 4096];
                    while let Some(n) = link_sender.next_send(&mut buf).await? {
                        framer.write(FrameType::Overnet, &buf[..n]).await?;
                    }
                    Ok::<_, Error>(())
                },
            )
            .await?;
            Ok(())
        },
    )
    .await?;

    Ok(())
}
