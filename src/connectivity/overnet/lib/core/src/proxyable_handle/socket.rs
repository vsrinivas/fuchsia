// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{IntoProxied, Message, Proxyable, RouterHolder, Serializer, IO};
use crate::async_quic::AsyncConnection;
use crate::framed_stream::MessageStats;
use anyhow::{format_err, Error};
use fidl::{AsyncSocket, HandleBased};
use fuchsia_zircon_status as zx_status;
use futures::io::{AsyncRead, AsyncWrite};
use futures::ready;
use std::cell::RefCell;
use std::pin::Pin;
use std::rc::Rc;
use std::task::{Context, Poll};

#[derive(Debug)]
pub(crate) struct Socket {
    socket: RefCell<AsyncSocket>,
}

impl Proxyable for Socket {
    type Message = SocketMessage;
    type Reader = SocketReader;
    type Writer = SocketWriter;

    fn from_fidl_handle(hdl: fidl::Handle) -> Result<Self, Error> {
        Ok(fidl::Socket::from_handle(hdl).into_proxied()?)
    }

    fn into_fidl_handle(self) -> Result<fidl::Handle, Error> {
        Ok(self
            .socket
            .into_inner()
            .into_zx_socket()
            .map_err(|_| format_err!("Failed to extract Socket from AsyncSocket"))?
            .into_handle())
    }
}

impl IntoProxied for fidl::Socket {
    type Proxied = Socket;
    fn into_proxied(self) -> Result<Socket, Error> {
        Ok(Socket { socket: RefCell::new(AsyncSocket::from_socket(self)?) })
    }
}

pub(crate) struct SocketReader;

impl IO for SocketReader {
    type Proxyable = Socket;
    fn new() -> Self {
        SocketReader
    }
    fn poll_io(
        &mut self,
        msg: &mut SocketMessage,
        socket: &Socket,
        fut_ctx: &mut Context<'_>,
    ) -> Poll<Result<(), zx_status::Status>> {
        const MIN_READ_LEN: usize = 65536;
        if msg.0.len() < MIN_READ_LEN {
            msg.0.resize(MIN_READ_LEN, 0u8);
        }
        let n = ready!(Pin::new(&mut *socket.socket.borrow_mut()).poll_read(fut_ctx, &mut msg.0))?;
        if n == 0 {
            return Poll::Ready(Err(zx_status::Status::PEER_CLOSED));
        }
        msg.0.truncate(n);
        Poll::Ready(Ok(()))
    }
}

pub(crate) struct SocketWriter;

impl IO for SocketWriter {
    type Proxyable = Socket;
    fn new() -> Self {
        SocketWriter
    }
    fn poll_io(
        &mut self,
        msg: &mut SocketMessage,
        socket: &Socket,
        fut_ctx: &mut Context<'_>,
    ) -> Poll<Result<(), zx_status::Status>> {
        let n = ready!(Pin::new(&mut *socket.socket.borrow_mut()).poll_write(fut_ctx, &mut msg.0))?;
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
        _: &AsyncConnection,
        _: &Rc<MessageStats>,
        _: &mut RouterHolder<'_>,
        _: &mut Context<'_>,
    ) -> Poll<Result<(), Error>> {
        std::mem::swap(bytes, &mut msg.0);
        Poll::Ready(Ok(()))
    }
}
