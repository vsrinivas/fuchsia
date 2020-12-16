// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{IntoProxied, Message, Proxyable, RouterHolder, Serializer, IO};
use crate::coding::{decode_fidl, encode_fidl};
use crate::peer::{AsyncConnection, MessageStats};
use anyhow::{Context as _, Error};
use fidl::{AsHandleRef, AsyncChannel, HandleBased};
use fidl_fuchsia_overnet_protocol::{ZirconChannelMessage, ZirconHandle};
use fuchsia_zircon_status as zx_status;
use futures::{prelude::*, ready};
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll};

pub(crate) struct Channel {
    chan: AsyncChannel,
}

impl std::fmt::Debug for Channel {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.chan.fmt(f)
    }
}

impl Proxyable for Channel {
    type Message = ChannelMessage;
    type Reader = ChannelReader;
    type Writer = ChannelWriter;

    fn from_fidl_handle(hdl: fidl::Handle) -> Result<Self, Error> {
        Ok(fidl::Channel::from_handle(hdl).into_proxied()?)
    }

    fn into_fidl_handle(self) -> Result<fidl::Handle, Error> {
        Ok(self.chan.into_zx_channel().into_handle())
    }
}

impl IntoProxied for fidl::Channel {
    type Proxied = Channel;
    fn into_proxied(self) -> Result<Channel, Error> {
        Ok(Channel { chan: AsyncChannel::from_channel(self)? })
    }
}

pub(crate) struct ChannelReader;

impl IO for ChannelReader {
    type Proxyable = Channel;
    fn new() -> ChannelReader {
        ChannelReader
    }
    fn poll_io(
        &mut self,
        msg: &mut ChannelMessage,
        channel: &Channel,
        fut_ctx: &mut Context<'_>,
    ) -> Poll<Result<(), zx_status::Status>> {
        channel.chan.read(fut_ctx, &mut msg.bytes, &mut msg.handles)
    }
}

pub(crate) struct ChannelWriter;

impl IO for ChannelWriter {
    type Proxyable = Channel;
    fn new() -> ChannelWriter {
        ChannelWriter
    }
    fn poll_io(
        &mut self,
        msg: &mut ChannelMessage,
        channel: &Channel,
        _: &mut Context<'_>,
    ) -> Poll<Result<(), zx_status::Status>> {
        Poll::Ready(Ok(channel.chan.write(&msg.bytes, &mut msg.handles)?))
    }
}

#[derive(Default, Debug)]
pub(crate) struct ChannelMessage {
    bytes: Vec<u8>,
    handles: Vec<fidl::Handle>,
}

impl Message for ChannelMessage {
    type Parser = ChannelMessageParser;
    type Serializer = ChannelMessageSerializer;
}

impl PartialEq for ChannelMessage {
    fn eq(&self, rhs: &Self) -> bool {
        if !self.handles.is_empty() {
            return false;
        }
        if !rhs.handles.is_empty() {
            return false;
        }
        return self.bytes == rhs.bytes;
    }
}

pub(crate) enum ChannelMessageParser {
    New,
    Pending {
        bytes: Vec<u8>,
        handles: Pin<Box<dyn 'static + Send + Future<Output = Result<Vec<fidl::Handle>, Error>>>>,
    },
    Done,
}

impl std::fmt::Debug for ChannelMessageParser {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ChannelMessageParser::New => "New",
            ChannelMessageParser::Pending { .. } => "Pending",
            ChannelMessageParser::Done => "Done",
        }
        .fmt(f)
    }
}

impl Serializer for ChannelMessageParser {
    type Message = ChannelMessage;
    fn new() -> Self {
        Self::New
    }
    fn poll_ser(
        &mut self,
        msg: &mut Self::Message,
        serialized: &mut Vec<u8>,
        conn: &AsyncConnection,
        stats: &Arc<MessageStats>,
        router: &mut RouterHolder<'_>,
        fut_ctx: &mut Context<'_>,
    ) -> Poll<Result<(), Error>> {
        log::trace!(
            "ChannelMessageParser::poll_ser: msg:{:?} serialized:{:?} self:{:?}",
            msg,
            serialized,
            self
        );
        match self {
            ChannelMessageParser::New => {
                let ZirconChannelMessage { mut bytes, handles: unbound_handles } =
                    decode_fidl(serialized)?;
                // Special case no handles case to avoid allocation dance
                if unbound_handles.is_empty() {
                    msg.handles.clear();
                    std::mem::swap(&mut msg.bytes, &mut bytes);
                    *self = ChannelMessageParser::Done;
                    return Poll::Ready(Ok(()));
                }
                let closure_conn = conn.clone();
                let closure_stats = stats.clone();
                let closure_router = router.get()?.clone();
                *self = ChannelMessageParser::Pending {
                    bytes,
                    handles: async move {
                        let mut handles = Vec::new();
                        for hdl in unbound_handles.into_iter() {
                            handles.push(
                                closure_router
                                    .clone()
                                    .recv_proxied(hdl, closure_conn.clone(), closure_stats.clone())
                                    .await?,
                            );
                        }
                        Ok(handles)
                    }
                    .boxed(),
                };
                self.poll_ser(msg, serialized, conn, stats, router, fut_ctx)
            }
            ChannelMessageParser::Pending { ref mut bytes, handles } => {
                let mut handles = ready!(handles.as_mut().poll(fut_ctx))?;
                std::mem::swap(&mut msg.handles, &mut handles);
                std::mem::swap(&mut msg.bytes, bytes);
                *self = ChannelMessageParser::Done;
                Poll::Ready(Ok(()))
            }
            ChannelMessageParser::Done => unreachable!(),
        }
    }
}

pub(crate) enum ChannelMessageSerializer {
    New,
    Pending(Pin<Box<dyn 'static + Send + Future<Output = Result<Vec<ZirconHandle>, Error>>>>),
    Done,
}

impl Serializer for ChannelMessageSerializer {
    type Message = ChannelMessage;
    fn new() -> Self {
        Self::New
    }
    fn poll_ser(
        &mut self,
        msg: &mut Self::Message,
        serialized: &mut Vec<u8>,
        conn: &AsyncConnection,
        stats: &Arc<MessageStats>,
        router: &mut RouterHolder<'_>,
        fut_ctx: &mut Context<'_>,
    ) -> Poll<Result<(), Error>> {
        log::trace!(
            "ChannelMessageSerializer::poll_ser: msg:{:?} serialized:{:?} self:{:?}",
            msg,
            serialized,
            match self {
                ChannelMessageSerializer::New => "New",
                ChannelMessageSerializer::Pending { .. } => "Pending",
                ChannelMessageSerializer::Done => "Done",
            }
        );
        match self {
            ChannelMessageSerializer::New => {
                let handles = std::mem::replace(&mut msg.handles, Vec::new());
                // Special case no handles case to avoid allocation dance
                if handles.is_empty() {
                    *serialized = encode_fidl(&mut ZirconChannelMessage {
                        bytes: std::mem::replace(&mut msg.bytes, Vec::new()),
                        handles: Vec::new(),
                    })?;
                    *self = ChannelMessageSerializer::Done;
                    return Poll::Ready(Ok(()));
                }
                let closure_conn = conn.clone();
                let closure_stats = stats.clone();
                let closure_router = router.get()?.clone();
                *self = ChannelMessageSerializer::Pending(
                    async move {
                        let mut send_handles = Vec::new();
                        for handle in handles {
                            // save for debugging
                            let raw_handle = handle.raw_handle();
                            send_handles.push(
                                closure_router
                                    .clone()
                                    .send_proxied(
                                        handle,
                                        closure_conn.clone(),
                                        closure_stats.clone(),
                                    )
                                    .await
                                    .with_context(|| format!("Sending handle {:?}", raw_handle))?,
                            );
                        }
                        Ok(send_handles)
                    }
                    .boxed(),
                );
                self.poll_ser(msg, serialized, conn, stats, router, fut_ctx)
            }
            ChannelMessageSerializer::Pending(handles) => {
                let handles = ready!(handles.as_mut().poll(fut_ctx))?;
                *serialized = encode_fidl(&mut ZirconChannelMessage {
                    bytes: std::mem::replace(&mut msg.bytes, Vec::new()),
                    handles,
                })?;
                *self = ChannelMessageSerializer::Done;
                Poll::Ready(Ok(()))
            }
            ChannelMessageSerializer::Done => unreachable!(),
        }
    }
}
