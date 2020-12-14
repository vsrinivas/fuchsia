// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Controls one link to another node over a zx socket.

use {
    crate::{
        router::Router,
        stream_framer::{new_deframer, new_framer, Format, LosslessBinary, LossyBinary, ReadBytes},
    },
    anyhow::{Context as _, Error},
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
    let (mut link_sender, mut link_receiver) = node.new_link(Box::new(move || {
        Some(fidl_fuchsia_overnet_protocol::LinkConfig::Socket(
            fidl_fuchsia_overnet_protocol::SocketLinkOptions {
                connection_label: options.connection_label.clone(),
                ..options.clone()
            },
        ))
    }));
    let _: ((), (), (), ()) = futures::future::try_join4(
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
                    return Ok::<_, Error>(());
                }
                deframer_write.write(&buf[..n]).await?;
            }
        },
        async move {
            loop {
                let frame = deframer.read().await?;
                if let ReadBytes::Framed(mut frame) = frame {
                    link_receiver.received_frame(frame.as_mut()).await;
                }
            }
        },
        async move {
            while let Some(frame) = link_sender.next_send().await {
                framer.write(frame.bytes()).await?;
            }
            Ok::<_, Error>(())
        },
    )
    .await?;

    Ok(())
}
