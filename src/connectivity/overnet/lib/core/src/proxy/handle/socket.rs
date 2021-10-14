// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{
    signals::Collector, IntoProxied, Message, Proxyable, ReadValue, RouterHolder, Serializer, IO,
};
use crate::coding;
use crate::peer::{MessageStats, PeerConnRef};
use anyhow::Error;
use fidl::{AsHandleRef, AsyncSocket, HandleBased, Peered, Signals};
use fuchsia_zircon_status as zx_status;
use futures::io::{AsyncRead, AsyncWrite};
use futures::ready;
use parking_lot::Mutex;
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll};

pub(crate) struct Socket {
    socket: Mutex<AsyncSocket>,
}

impl std::fmt::Debug for Socket {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        "Socket".fmt(f)
    }
}

impl Proxyable for Socket {
    type Message = SocketMessage;
    type Reader = SocketReader;
    type Writer = SocketWriter;

    fn from_fidl_handle(hdl: fidl::Handle) -> Result<Self, Error> {
        Ok(fidl::Socket::from_handle(hdl).into_proxied()?)
    }

    fn into_fidl_handle(self) -> Result<fidl::Handle, Error> {
        Ok(self.socket.into_inner().into_zx_socket().into_handle())
    }

    fn signal_peer(&self, clear: Signals, set: Signals) -> Result<(), Error> {
        let socket = self.socket.lock();
        let socket: &fidl::Socket = socket.as_ref();
        socket.signal_peer(clear, set)?;
        Ok(())
    }
}

impl IntoProxied for fidl::Socket {
    type Proxied = Socket;
    fn into_proxied(self) -> Result<Socket, Error> {
        Ok(Socket { socket: Mutex::new(AsyncSocket::from_socket(self)?) })
    }
}

pub(crate) struct SocketReader {
    collector: Collector,
}

impl IO for SocketReader {
    type Proxyable = Socket;
    type Output = ReadValue;
    fn new() -> Self {
        SocketReader { collector: Default::default() }
    }
    fn poll_io(
        &mut self,
        msg: &mut SocketMessage,
        socket: &Socket,
        fut_ctx: &mut Context<'_>,
    ) -> Poll<Result<ReadValue, zx_status::Status>> {
        const MIN_READ_LEN: usize = 65536;
        if msg.0.len() < MIN_READ_LEN {
            msg.0.resize(MIN_READ_LEN, 0u8);
        }
        let mut socket = socket.socket.lock();
        let read_result = (|| {
            let n = ready!(Pin::new(&mut *socket).poll_read(fut_ctx, &mut msg.0))?;
            if n == 0 {
                return Poll::Ready(Err(zx_status::Status::PEER_CLOSED));
            }
            msg.0.truncate(n);
            Poll::Ready(Ok(()))
        })();
        self.collector.after_read(fut_ctx, socket.as_handle_ref(), read_result)
    }
}

pub(crate) struct SocketWriter;

impl IO for SocketWriter {
    type Proxyable = Socket;
    type Output = ();
    fn new() -> Self {
        SocketWriter
    }
    fn poll_io(
        &mut self,
        msg: &mut SocketMessage,
        socket: &Socket,
        fut_ctx: &mut Context<'_>,
    ) -> Poll<Result<(), zx_status::Status>> {
        let mut socket = socket.socket.lock();
        let n = ready!(Pin::new(&mut *socket).poll_write(fut_ctx, &mut msg.0))?;
        if n == msg.0.len() {
            Poll::Ready(Ok(()))
        } else {
            msg.0.drain(..n);
            Poll::Pending
        }
    }
}

#[derive(Default, PartialEq)]
pub(crate) struct SocketMessage(Vec<u8>);

impl std::fmt::Debug for SocketMessage {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0.fmt(f)
    }
}

impl Message for SocketMessage {
    type Parser = SocketMessageSerializer;
    type Serializer = SocketMessageSerializer;
}

#[derive(Debug)]
pub(crate) struct SocketMessageSerializer;

impl Serializer for SocketMessageSerializer {
    type Message = SocketMessage;
    fn new() -> SocketMessageSerializer {
        SocketMessageSerializer
    }
    fn poll_ser(
        &mut self,
        msg: &mut SocketMessage,
        bytes: &mut Vec<u8>,
        _: PeerConnRef<'_>,
        _: &Arc<MessageStats>,
        _: &mut RouterHolder<'_>,
        _: &mut Context<'_>,
        _: coding::Context,
    ) -> Poll<Result<(), Error>> {
        std::mem::swap(bytes, &mut msg.0);
        Poll::Ready(Ok(()))
    }
}
