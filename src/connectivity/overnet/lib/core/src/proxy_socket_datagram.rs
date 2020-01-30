// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    async_quic::{AsyncQuicStreamReader, AsyncQuicStreamWriter},
    framed_stream::{framed, FrameType, FramedStreamReader, FramedStreamWriter, MessageStats},
    future_help::log_errors,
    runtime::spawn,
};
use anyhow::{bail, Error};
use fidl::{AsyncSocket, Socket};
use futures::io::{AsyncReadExt, AsyncWriteExt, ReadHalf, WriteHalf};
use std::rc::Rc;

const MAX_DATAGRAM_SIZE: usize = 65536;

pub fn spawn_socket_datagram_proxy(
    sock: Socket,
    stream_io: (AsyncQuicStreamWriter, AsyncQuicStreamReader),
    stats: Rc<MessageStats>,
) -> Result<(), Error> {
    let (sock_reader, sock_sender) = AsyncReadExt::split(AsyncSocket::from_socket(sock)?);
    let (stream_writer, stream_reader) = framed(stream_io);
    spawn(log_errors(
        socket_to_stream(sock_reader, stream_writer, stats),
        "socket_to_stream failed",
    ));
    spawn(log_errors(stream_to_socket(stream_reader, sock_sender), "stream_to_socket failed"));
    Ok(())
}

async fn socket_to_stream(
    mut sock: ReadHalf<AsyncSocket>,
    mut stream: FramedStreamWriter,
    stats: Rc<MessageStats>,
) -> Result<(), Error> {
    let mut buf = [0u8; MAX_DATAGRAM_SIZE + 1];
    loop {
        let n = sock.read(&mut buf).await?;
        if n == 0 {
            stream.send(FrameType::Data, &[], true, &*stats).await?;
            return Ok(());
        }
        if n > MAX_DATAGRAM_SIZE {
            bail!("Frame too large");
        }
        stream.send(FrameType::Data, &buf, false, &*stats).await?;
    }
}

async fn stream_to_socket(
    mut stream: FramedStreamReader,
    mut sock: WriteHalf<AsyncSocket>,
) -> Result<(), Error> {
    loop {
        let (frame_type, msg, fin) = stream.next().await?;
        match frame_type {
            FrameType::Data => {
                sock.write(&msg).await?;
                if fin {
                    return Ok(());
                }
            }
        }
    }
}
