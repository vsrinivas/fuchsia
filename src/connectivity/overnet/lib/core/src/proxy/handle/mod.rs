// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod channel;
mod event_pair;
mod signals;
mod socket;

use super::stream::{Frame, StreamReaderBinder, StreamWriter};
use crate::coding;
use crate::peer::{FramedStreamReader, MessageStats, PeerConnRef};
use crate::router::Router;
use anyhow::{bail, format_err, Error};
use fidl::Signals;
use fidl_fuchsia_overnet_protocol::SignalUpdate;
use fuchsia_zircon_status as zx_status;
use futures::{future::poll_fn, prelude::*, task::noop_waker_ref};
use std::sync::{Arc, Weak};
use std::task::{Context, Poll};

/// Holds a reference to a router.
/// We start out `Unused` with a weak reference to the router, but various methods
/// need said router, and so we can transition to `Used` with a reference when the router
/// is needed.
/// Saves some repeated upgrading of weak to arc.
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

/// Perform some IO operation on a handle.
pub(crate) trait IO: Send {
    type Proxyable: Proxyable;
    type Output;
    fn new() -> Self;
    fn poll_io(
        &mut self,
        msg: &mut <Self::Proxyable as Proxyable>::Message,
        proxyable: &Self::Proxyable,
        fut_ctx: &mut Context<'_>,
    ) -> Poll<Result<Self::Output, zx_status::Status>>;
}

/// Serializer defines how to read or write a message to a QUIC stream.
/// They are usually defined in pairs (one reader, one writer).
/// In some cases those implementations end up being the same and we leverage that to improve
/// footprint.
pub(crate) trait Serializer: Send {
    type Message;
    fn new() -> Self;
    fn poll_ser(
        &mut self,
        msg: &mut Self::Message,
        bytes: &mut Vec<u8>,
        conn: PeerConnRef<'_>,
        stats: &Arc<MessageStats>,
        router: &mut RouterHolder<'_>,
        fut_ctx: &mut Context<'_>,
        coding_context: coding::Context,
    ) -> Poll<Result<(), Error>>;
}

/// A proxyable message - defines how to parse/serialize itself, and gets pulled
/// in by Proxyable to also define how to send/receive itself on the right kind of handle.
pub(crate) trait Message: Send + Sized + Default + PartialEq + std::fmt::Debug {
    /// How to parse this message type from bytes.
    type Parser: Serializer<Message = Self> + std::fmt::Debug;
    /// How to turn this message into wire bytes.
    type Serializer: Serializer<Message = Self>;
}

/// The result of an IO read - either a message was received, or a signal.
pub(crate) enum ReadValue {
    Message,
    SignalUpdate(SignalUpdate),
}

/// An object that can be proxied.
pub(crate) trait Proxyable: Send + Sync + Sized + std::fmt::Debug {
    /// The type of message exchanged by this handle.
    /// This transitively also brings in types encoding how to parse/serialize messages to the
    /// wire.
    type Message: Message;
    /// A type that can be used for communicating messages from the handle to the proxy code.
    type Reader: IO<Proxyable = Self, Output = ReadValue>;
    /// A type that can be used for communicating messages from the proxy code to the handle.
    type Writer: IO<Proxyable = Self, Output = ()>;

    /// Convert a FIDL handle into a proxyable instance (or fail).
    fn from_fidl_handle(hdl: fidl::Handle) -> Result<Self, Error>;
    /// Collapse this Proxyable instance back to the underlying FIDL handle (or fail).
    fn into_fidl_handle(self) -> Result<fidl::Handle, Error>;
    /// Clear/set signals on this handle's peer.
    fn signal_peer(&self, clear: Signals, set: Signals) -> Result<(), Error>;
}

pub(crate) trait IntoProxied {
    type Proxied: Proxyable;
    fn into_proxied(self) -> Result<Self::Proxied, Error>;
}

/// Wraps a Proxyable, adds some convenience values, and provides a nicer API.
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

    /// Write `msg` to the handle.
    pub(crate) async fn write(&self, msg: &mut Hdl::Message) -> Result<(), zx_status::Status> {
        self.handle_io(msg, Hdl::Writer::new()).await
    }

    /// Attempt to read one `msg` from the handle.
    /// Return Ok(Message) if a message was read.
    /// Return Ok(SignalUpdate) if a signal was instead read.
    /// Return Err(_) if an error occurred.
    pub(crate) fn read<'a>(
        &'a self,
        msg: &'a mut Hdl::Message,
    ) -> impl 'a + Future<Output = Result<ReadValue, zx_status::Status>> + Unpin {
        self.handle_io(msg, Hdl::Reader::new())
    }

    pub(crate) fn router(&self) -> &Weak<Router> {
        &self.router
    }

    pub(crate) fn stats(&self) -> &Arc<MessageStats> {
        &self.stats
    }

    /// Given a signal update from the wire, apply it to the underlying handle (signalling
    /// the peer and completing the loop).
    pub(crate) fn apply_signal_update(&self, signal_update: SignalUpdate) -> Result<(), Error> {
        if let Some(assert_signals) = signal_update.assert_signals {
            self.hdl
                .signal_peer(Signals::empty(), self::signals::from_wire_signals(assert_signals))?
        }
        Ok(())
    }

    fn handle_io<'a, I: 'a + IO<Proxyable = Hdl>>(
        &'a self,
        msg: &'a mut Hdl::Message,
        mut io: I,
    ) -> impl 'a + Future<Output = Result<I::Output, zx_status::Status>> + Unpin {
        poll_fn(move |fut_ctx| io.poll_io(msg, &self.hdl, fut_ctx))
    }

    /// Drain all remaining messages from this handle and write them to `stream_writer`.
    /// Assumes that nothing else is writing to the handle, so that getting Poll::Pending on read
    /// is a signal that all messages have been read.
    pub(crate) async fn drain_to_stream(
        &self,
        stream_writer: &mut StreamWriter<Hdl::Message>,
    ) -> Result<(), Error> {
        let mut message = Default::default();
        let mut ctx = Context::from_waker(noop_waker_ref());
        loop {
            let pr = self.read(&mut message).poll_unpin(&mut ctx);
            match pr {
                Poll::Pending => return Ok(()),
                Poll::Ready(Err(e)) => return Err(e.into()),
                Poll::Ready(Ok(ReadValue::Message)) => {
                    stream_writer.send_data(&mut message).await?
                }
                Poll::Ready(Ok(ReadValue::SignalUpdate(signal_update))) => {
                    stream_writer.send_signal(signal_update).await?
                }
            }
        }
    }

    /// Drain all messages on a stream into this handle.
    pub(crate) async fn drain_stream_to_handle(
        self,
        drain_stream: FramedStreamReader,
    ) -> Result<fidl::Handle, Error> {
        let mut drain_stream = drain_stream.bind(&self);
        loop {
            match drain_stream.next().await? {
                Frame::Data(message) => self.write(message).await?,
                Frame::SignalUpdate(signal_update) => self.apply_signal_update(signal_update)?,
                Frame::EndTransfer => return Ok(self.hdl.into_fidl_handle()?),
                Frame::Hello => bail!("Hello frame disallowed on drain streams"),
                Frame::BeginTransfer(_, _) => bail!("BeginTransfer on drain stream"),
                Frame::AckTransfer => bail!("AckTransfer on drain stream"),
                Frame::Shutdown(r) => bail!("Stream shutdown during drain: {:?}", r),
            }
        }
    }
}
