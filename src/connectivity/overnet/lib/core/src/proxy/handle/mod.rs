// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod channel;
mod socket;

use super::stream::{Frame, StreamReaderBinder, StreamWriter};
use crate::peer::{AsyncConnection, FramedStreamReader, MessageStats};
use crate::router::Router;
use anyhow::{bail, format_err, Error};
use fuchsia_zircon_status as zx_status;
use futures::{future::poll_fn, prelude::*, task::noop_waker_ref};
use std::sync::{Arc, Weak};
use std::task::{Context, Poll};

#[derive(Clone)]
pub(crate) enum RouterHolder<'a> {
    Unused(&'a Weak<Router>),
    Used(Arc<Router>),
}

impl<'a> std::fmt::Debug for RouterHolder<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            RouterHolder::Unused(_) => f.write_str("Unused"),
            RouterHolder::Used(r) => write!(f, "Used({:?})", r.node_id()),
        }
    }
}

impl<'a> RouterHolder<'a> {
    pub(crate) fn get(&mut self) -> Result<&Arc<Router>, Error> {
        match self {
            RouterHolder::Used(ref r) => Ok(r),
            RouterHolder::Unused(r) => {
                *self = RouterHolder::Used(
                    Weak::upgrade(r).ok_or_else(|| format_err!("Router is closed"))?,
                );
                self.get()
            }
        }
    }
}

pub(crate) trait IO: Send {
    type Proxyable: Proxyable;
    fn new() -> Self;
    fn poll_io(
        &mut self,
        msg: &mut <Self::Proxyable as Proxyable>::Message,
        proxyable: &Self::Proxyable,
        fut_ctx: &mut Context<'_>,
    ) -> Poll<Result<(), zx_status::Status>>;
}

pub(crate) trait Serializer: Send {
    type Message;
    fn new() -> Self;
    fn poll_ser(
        &mut self,
        msg: &mut Self::Message,
        bytes: &mut Vec<u8>,
        conn: &AsyncConnection,
        stats: &Arc<MessageStats>,
        router: &mut RouterHolder<'_>,
        fut_ctx: &mut Context<'_>,
    ) -> Poll<Result<(), Error>>;
}

pub(crate) trait Message: Send + Sized + Default + PartialEq + std::fmt::Debug {
    type Parser: Serializer<Message = Self> + std::fmt::Debug;
    type Serializer: Serializer<Message = Self>;
}

pub(crate) trait Proxyable: Send + Sync + Sized + std::fmt::Debug {
    type Message: Message;
    type Reader: IO<Proxyable = Self>;
    type Writer: IO<Proxyable = Self>;

    fn from_fidl_handle(hdl: fidl::Handle) -> Result<Self, Error>;

    fn into_fidl_handle(self) -> Result<fidl::Handle, Error>;
}

pub(crate) trait IntoProxied {
    type Proxied: Proxyable;
    fn into_proxied(self) -> Result<Self::Proxied, Error>;
}

pub(crate) struct ProxyableHandle<Hdl: Proxyable> {
    hdl: Hdl,
    router: Weak<Router>,
    stats: Arc<MessageStats>,
}

impl<Hdl: Proxyable> std::fmt::Debug for ProxyableHandle<Hdl> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}#{:?}", self.hdl, Weak::upgrade(&self.router).map(|r| r.node_id()))
    }
}

impl<Hdl: Proxyable> ProxyableHandle<Hdl> {
    pub(crate) fn new(hdl: Hdl, router: Weak<Router>, stats: Arc<MessageStats>) -> Self {
        Self { hdl, router, stats }
    }

    pub(crate) fn into_fidl_handle(self) -> Result<fidl::Handle, Error> {
        self.hdl.into_fidl_handle()
    }

    pub(crate) async fn write(&self, msg: &mut Hdl::Message) -> Result<(), zx_status::Status> {
        self.handle_io(msg, Hdl::Writer::new()).await
    }

    pub(crate) fn read<'a>(
        &'a self,
        msg: &'a mut Hdl::Message,
    ) -> impl 'a + Future<Output = Result<(), zx_status::Status>> + Unpin {
        self.handle_io(msg, Hdl::Reader::new())
    }

    pub(crate) fn router(&self) -> &Weak<Router> {
        &self.router
    }

    pub(crate) fn stats(&self) -> &Arc<MessageStats> {
        &self.stats
    }

    pub(crate) fn hdl(&self) -> &Hdl {
        &self.hdl
    }

    fn handle_io<'a>(
        &'a self,
        msg: &'a mut Hdl::Message,
        mut io: impl 'a + IO<Proxyable = Hdl>,
    ) -> impl 'a + Future<Output = Result<(), zx_status::Status>> + Unpin {
        poll_fn(move |fut_ctx| io.poll_io(msg, &self.hdl, fut_ctx))
    }

    pub(crate) async fn drain_to_stream(
        &self,
        stream_writer: &mut StreamWriter<Hdl::Message>,
    ) -> Result<(), Error> {
        let mut message = Default::default();
        let mut ctx = Context::from_waker(noop_waker_ref());
        loop {
            match self.read(&mut message).poll_unpin(&mut ctx) {
                Poll::Pending => return Ok(()),
                Poll::Ready(x) => x?,
            }
            stream_writer.send_data(&mut message).await?;
        }
    }

    pub(crate) async fn drain_stream_to_handle(
        self,
        drain_stream: FramedStreamReader,
    ) -> Result<fidl::Handle, Error> {
        let mut drain_stream = drain_stream.bind(&self);
        loop {
            match drain_stream.next().await? {
                Frame::Data(message) => self.write(message).await?,
                Frame::EndTransfer => return Ok(self.hdl.into_fidl_handle()?),
                Frame::Hello => bail!("Hello frame disallowed on drain streams"),
                Frame::BeginTransfer(_, _) => bail!("BeginTransfer on drain stream"),
                Frame::AckTransfer => bail!("AckTransfer on drain stream"),
                Frame::Shutdown(r) => bail!("Stream shutdown during drain: {:?}", r),
            }
        }
    }
}
