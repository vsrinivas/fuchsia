// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    async_quic::{AsyncQuicStreamReader, AsyncQuicStreamWriter, StreamProperties},
    framed_stream::{framed, FrameType, FramedStreamReader, FramedStreamWriter, MessageStats},
    future_help::log_errors,
    runtime::spawn,
};
use anyhow::{bail, format_err, Error};
use fidl::{AsyncSocket, Socket};
use futures::io::{AsyncReadExt, AsyncWriteExt, ReadHalf, WriteHalf};
use std::rc::Rc;

pub fn spawn_socket_stream_proxy(
    sock: Socket,
    stream_io: (AsyncQuicStreamWriter, AsyncQuicStreamReader),
    stats: Rc<MessageStats>,
) -> Result<(), Error> {
    let (sock_reader, sock_sender) = AsyncReadExt::split(AsyncSocket::from_socket(sock)?);
    let (stream_writer, stream_reader) = framed(stream_io);
    spawn(log_errors(
        stream_to_socket(stream_reader, sock_sender, sock_reader, stream_writer, stats),
        "stream_to_socket failed",
    ));
    Ok(())
}

async fn socket_to_stream(
    mut sock: ReadHalf<AsyncSocket>,
    mut stream: FramedStreamWriter,
    stats: Rc<MessageStats>,
) -> Result<(), Error> {
    if stream.is_initiator() {
        log::trace!("Send hello message");
        stream.send(FrameType::Hello, &[], false, &*stats).await?;
        log::trace!("Sent hello message");
    }
    let mut buf = [0u8; 4096];
    loop {
        let n = sock.read(&mut buf).await?;
        log::trace!("socket_to_stream gets bytes {:?}", &buf[..n]);
        if n == 0 {
            stream.send(FrameType::Data, &[], true, &*stats).await?;
            return Ok(());
        }
        stream.send(FrameType::Data, &buf[..n], false, &*stats).await?;
    }
}

async fn stream_to_socket(
    mut stream: FramedStreamReader,
    mut sock: WriteHalf<AsyncSocket>,

    sock_reader: ReadHalf<AsyncSocket>,
    stream_writer: FramedStreamWriter,
    stats: Rc<MessageStats>,
) -> Result<(), Error> {
    if !stream.is_initiator() {
        log::trace!("Await hello message");
        stream
            .expect(FrameType::Hello, None, |msg| {
                if msg.len() == 0 {
                    Ok(())
                } else {
                    Err(format_err!("Expected empty hello message"))
                }
            })
            .await?;
        log::trace!("Got hello message");
    }
    spawn(log_errors(
        socket_to_stream(sock_reader, stream_writer, stats),
        "socket_to_stream failed",
    ));
    loop {
        let (frame_type, msg, fin) = stream.next().await?;
        log::trace!("stream_to_socket gets {:?} {:?} fin={:?}", frame_type, msg, fin);
        match frame_type {
            FrameType::Hello => bail!("Should not see Hello frames in regular flow"),
            FrameType::Data => {
                sock.write(&msg).await?;
                if fin {
                    return Ok(());
                }
            }
        }
    }
}
