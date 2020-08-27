// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Controls one link to another node over a zx socket.

use {
    crate::{
        coding::{decode_fidl, encode_fidl},
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
    socket: fidl::Socket,
    options: fidl_fuchsia_overnet_protocol::SocketLinkOptions,
) -> Result<(), Error> {
    let duration_per_byte = if let Some(n) = options.bytes_per_second {
        Some(std::cmp::max(
            Duration::from_micros(10),
            Duration::from_secs(1)
                .checked_div(n)
                .ok_or_else(|| anyhow::format_err!("Division failed: 1 second / {}", n))?,
        ))
    } else {
        None
    };

    let connection_label = options.connection_label.clone();

    log::trace!("Begin handshake: connection_label:{:?} socket:{:?}", connection_label, socket);
    let dbgid = (node.node_id(), crate::router::generate_node_id().0);

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
                log::trace!("{:?} write bytes {:?}", dbgid, msg.len());
                tx_bytes.write(&msg).await?;
                log::trace!("{:?} wrote bytes", dbgid);
            }
        },
        async move {
            let mut buf = [0u8; 4096];
            loop {
                let n = rx_bytes.read(&mut buf).await?;
                if n == 0 {
                    log::trace!("{:?} finished reading bytes", dbgid);
                    return Ok(());
                }
                log::trace!("{:?} read bytes {:?}", dbgid, n);
                deframer_write.write(&buf[..n]).await?;
                log::trace!("{:?} queued deframe bytes", dbgid);
            }
        },
        async move {
            let mut greeting = StreamSocketGreeting {
                magic_string: Some(GREETING_STRING.to_string()),
                node_id: Some(node.node_id().into()),
                connection_label,
                key: None,
            };
            framer
                .write(
                    FrameType::OvernetHello,
                    encode_fidl(&mut greeting).context("encoding greeting")?.as_slice(),
                )
                .await
                .context("queue greeting")?;
            log::trace!("{:?} Wrote greeting: {:?}", dbgid, greeting);
            // Wait for first frame
            let (frame_type, mut greeting_bytes) = deframer.read().await?;
            if frame_type != Some(FrameType::OvernetHello) {
                bail!("Expected OvernetHello frame, got {:?}", frame_type);
            }
            let greeting = decode_fidl::<StreamSocketGreeting>(greeting_bytes.as_mut())
                .context("decoding greeting")?;
            log::trace!("{:?} Got greeting: {:?}", dbgid, greeting);
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
            log::trace!("{:?} Handshake complete, creating link", dbgid);
            let (link_sender, link_receiver) = node
                .new_link(
                    node_id.into(),
                    Box::new(move || {
                        Some(fidl_fuchsia_overnet_protocol::LinkConfig::Socket(
                            fidl_fuchsia_overnet_protocol::SocketLinkOptions {
                                connection_label: options.connection_label.clone(),
                                ..options
                            },
                        ))
                    }),
                )
                .await
                .context("creating link")?;
            log::trace!("{:?} Running link", dbgid);
            let _: ((), ()) = futures::future::try_join(
                async move {
                    loop {
                        let (frame_type, mut frame) = deframer.read().await?;
                        log::trace!("{:?} got frame {:?} {:?}", dbgid, frame_type, frame.len());
                        if frame_type != Some(FrameType::Overnet) {
                            log::warn!("Skip frame of type {:?}", frame_type);
                            continue;
                        }
                        if let Err(err) = link_receiver.received_packet(frame.as_mut()).await {
                            log::warn!("Error reading packet: {:?}", err);
                        }
                        log::trace!("{:?} handled frame", dbgid);
                    }
                },
                async move {
                    let mut buf = [0u8; 4096];
                    while let Some(n) = link_sender.next_send(&mut buf).await? {
                        log::trace!("{:?} send frame {:?}", dbgid, n);
                        framer.write(FrameType::Overnet, &buf[..n]).await?;
                        log::trace!("{:?} sent frame", dbgid);
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
