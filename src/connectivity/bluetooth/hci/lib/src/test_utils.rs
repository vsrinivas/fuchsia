// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::transport::{
    HwTransport, HwTransportBuilder, IncomingPacket, IncomingPacketToken, OutgoingPacket,
};
use {
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        channel::mpsc::{UnboundedReceiver as AsyncReceiver, UnboundedSender as AsyncSender},
        stream::FusedStream,
        Sink, Stream,
    },
    std::{
        pin::Pin,
        task::{Context, Poll},
    },
};

/// An owned equivalent of the `OutgoingPacket` struct that can be sent through the TestTransport to
/// tests.
#[derive(Debug, PartialEq)]
pub enum OwnedOutgoingPacket {
    Cmd(Vec<u8>),
    Acl(Vec<u8>),
}

impl<'a> From<OutgoingPacket<'a>> for OwnedOutgoingPacket {
    fn from(p: OutgoingPacket<'a>) -> OwnedOutgoingPacket {
        match p {
            OutgoingPacket::Cmd(buf) => OwnedOutgoingPacket::Cmd(buf.to_vec()),
            OutgoingPacket::Acl(buf) => OwnedOutgoingPacket::Acl(buf.to_vec()),
        }
    }
}

pub(crate) struct TestTransport {
    rx: AsyncReceiver<IncomingPacket>,
    tx: AsyncSender<OwnedOutgoingPacket>,
    next_packet: Option<IncomingPacket>,
    pub unbound: async_utils::event::Event,
}

impl TestTransport {
    pub fn new(
    ) -> (Box<TestTransport>, AsyncSender<IncomingPacket>, AsyncReceiver<OwnedOutgoingPacket>) {
        let (in_tx, in_rx) = futures::channel::mpsc::unbounded();
        let (out_tx, out_rx) = futures::channel::mpsc::unbounded();
        let unbound = async_utils::event::Event::new();
        (
            Box::new(TestTransport { rx: in_rx, tx: out_tx, next_packet: None, unbound }),
            in_tx,
            out_rx,
        )
    }
}

impl HwTransportBuilder for TestTransport {
    fn build(self: Box<Self>) -> Result<Box<dyn HwTransport>, zx::Status> {
        Ok(self)
    }
}

impl Stream for TestTransport {
    type Item = IncomingPacketToken;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        match Pin::new(&mut self.rx).poll_next(cx) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(None) => Poll::Ready(None),
            Poll::Ready(pkt) => {
                self.next_packet = pkt;
                Poll::Ready(Some(IncomingPacketToken::mint_in_test()))
            }
        }
    }
}

impl FusedStream for TestTransport {
    fn is_terminated(&self) -> bool {
        self.rx.is_terminated()
    }
}

impl<'a> Sink<OutgoingPacket<'a>> for TestTransport {
    type Error = zx::Status;
    fn poll_ready(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(Ok(()))
    }

    fn start_send(self: Pin<&mut Self>, item: OutgoingPacket<'_>) -> Result<(), Self::Error> {
        self.tx.unbounded_send(item.into()).map_err(|_| zx::Status::INTERNAL)
    }

    fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(Ok(()))
    }

    fn poll_close(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(Ok(()))
    }
}

impl Unpin for TestTransport {}
impl HwTransport for TestTransport {
    fn take_incoming(&mut self, _proof: IncomingPacketToken, _buffer: Vec<u8>) -> IncomingPacket {
        self.next_packet.take().expect("packet should be ready in test transport")
    }
    unsafe fn unbind(&mut self) {
        self.unbound.signal();
    }
}

/// Store all snoop messages for later inspection.
pub(crate) struct SnoopSink {
    inner: std::sync::Arc<futures::lock::Mutex<Vec<Vec<u8>>>>,
}

impl SnoopSink {
    /// Create a `SnoopSink` and spawn a background task to read data off the channel
    pub fn spawn_from_channel(c: zx::Channel) -> SnoopSink {
        let this = SnoopSink { inner: Default::default() };
        let inner_clone = this.inner.clone();
        let fut = async move {
            let c = fasync::Channel::from_channel(c).expect("Create async channel");
            loop {
                let mut buf = zx::MessageBuf::new();
                let read = c.recv_msg(&mut buf);
                read.await.expect("Error in receiving message. Perhaps channel closed?");
                inner_clone.lock().await.push(buf.bytes().to_vec());
            }
        };
        fasync::Task::spawn(fut).detach();
        this
    }

    /// Return a copy of the snoop messages data.
    pub async fn data(&self) -> Vec<Vec<u8>> {
        self.inner.lock().await.clone()
    }
}
