// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::handle::{Message, Proxyable, ProxyableHandle, RouterHolder, Serializer};
use crate::coding::{decode_fidl, encode_fidl};
use crate::labels::{NodeId, TransferKey};
use crate::peer::{
    AsyncConnection, FrameType, FramedStreamReader, FramedStreamWriter, MessageStats,
    ReadNextFrame, StreamProperties,
};
use crate::router::Router;
use anyhow::{format_err, Context as _, Error};
use fidl_fuchsia_overnet_protocol::{BeginTransfer, Empty, StreamControl};
use fuchsia_zircon_status as zx_status;
use futures::{future::poll_fn, prelude::*, ready};
use std::pin::Pin;
use std::sync::{Arc, Weak};
use std::task::{Context, Poll};

pub(crate) struct StreamWriter<Msg: Message> {
    stream: FramedStreamWriter,
    send_buffer: Vec<u8>,
    stats: Arc<MessageStats>,
    router: Weak<Router>,
    closed: bool,
    _phantom_msg: std::marker::PhantomData<Msg>,
}

impl<Msg: Message> StreamProperties for StreamWriter<Msg> {
    fn id(&self) -> u64 {
        self.stream.id()
    }

    fn conn(&self) -> &AsyncConnection {
        self.stream.conn()
    }
}

impl<Msg: Message> StreamWriter<Msg> {
    pub(crate) async fn send_data(&mut self, msg: &mut Msg) -> Result<(), Error> {
        assert_ne!(self.closed, true);
        let mut s = Msg::Serializer::new();
        let send_buffer = &mut self.send_buffer;
        let conn = self.stream.conn();
        let mut rh = RouterHolder::Unused(&self.router);
        let stats = &self.stats;
        poll_fn(|fut_ctx| s.poll_ser(msg, send_buffer, conn, stats, &mut rh, fut_ctx))
            .await
            .with_context(|| format_err!("Serializing message {:?}", msg))?;
        self.stream
            .send(FrameType::Data, &self.send_buffer, false, &self.stats)
            .await
            .with_context(|| format_err!("sending data {:?} ser={:?}", msg, self.send_buffer))
    }

    async fn send_control(&mut self, mut msg: StreamControl, fin: bool) -> Result<(), Error> {
        assert_ne!(self.closed, true);
        let msg = encode_fidl(&mut msg)
            .with_context(|| format_err!("encoding control message {:?}", msg))?;
        if fin {
            self.closed = true;
        }
        self.stream
            .send(FrameType::Control, msg.as_slice(), fin, &self.stats)
            .await
            .with_context(|| format_err!("sending control message {:?}", msg))
    }

    pub(crate) async fn send_ack_transfer(mut self) -> Result<(), Error> {
        Ok(self.send_control(StreamControl::AckTransfer(Empty {}), true).await?)
    }

    pub(crate) async fn send_end_transfer(mut self) -> Result<(), Error> {
        Ok(self.send_control(StreamControl::EndTransfer(Empty {}), true).await?)
    }

    pub(crate) async fn send_begin_transfer(
        &mut self,
        new_destination_node: NodeId,
        transfer_key: TransferKey,
    ) -> Result<(), Error> {
        Ok(self
            .send_control(
                StreamControl::BeginTransfer(BeginTransfer {
                    new_destination_node: new_destination_node.into(),
                    transfer_key,
                }),
                false,
            )
            .await?)
    }

    pub(crate) async fn send_hello(&mut self) -> Result<(), Error> {
        self.stream
            .send(FrameType::Hello, &[], false, &self.stats)
            .await
            .with_context(|| format_err!("sending hello"))
    }

    pub(crate) async fn send_shutdown(
        mut self,
        r: Result<(), zx_status::Status>,
    ) -> Result<(), Error> {
        self.send_control(
            StreamControl::Shutdown(
                match r {
                    Ok(()) => zx_status::Status::OK,
                    Err(s) => s,
                }
                .into_raw(),
            ),
            true,
        )
        .await
    }
}

pub(crate) trait StreamWriterBinder {
    fn bind<Msg: Message, H: Proxyable<Message = Msg>>(
        self,
        hdl: &ProxyableHandle<H>,
    ) -> StreamWriter<Msg>;
}

impl StreamWriterBinder for FramedStreamWriter {
    fn bind<Msg: Message, H: Proxyable<Message = Msg>>(
        self,
        hdl: &ProxyableHandle<H>,
    ) -> StreamWriter<Msg> {
        StreamWriter {
            stream: self,
            send_buffer: Vec::new(),
            stats: hdl.stats().clone(),
            router: hdl.router().clone(),
            closed: false,
            _phantom_msg: std::marker::PhantomData,
        }
    }
}

#[derive(PartialEq, Debug)]
pub(crate) enum Frame<'a, Msg: Message> {
    Hello,
    Data(&'a mut Msg),
    BeginTransfer(NodeId, TransferKey),
    AckTransfer,
    EndTransfer,
    Shutdown(Result<(), zx_status::Status>),
}

#[derive(Debug)]
pub(crate) struct StreamReader<Msg: Message> {
    conn: AsyncConnection,
    stream: FramedStreamReader,
    incoming_message: Msg,
    router: Weak<Router>,
    stats: Arc<MessageStats>,
    state: ReadNextState<Msg::Parser>,
}

#[derive(Debug)]
pub(crate) struct ReadNext<'a, Msg: Message> {
    next_frame: Option<ReadNextFrame<'a>>,
    state: &'a mut ReadNextState<Msg::Parser>,
    incoming_message: Option<&'a mut Msg>,
    stats: &'a Arc<MessageStats>,
    conn: &'a AsyncConnection,
    router_holder: RouterHolder<'a>,
}

#[derive(Debug)]
enum ReadNextState<Parser> {
    Reading,
    DeserializingData(Vec<u8>, Parser),
}

impl<'a, Msg: Message> ReadNext<'a, Msg> {
    fn poll_inner(&mut self, ctx: &mut Context<'_>) -> Poll<Result<Frame<'a, Msg>, Error>> {
        loop {
            return Poll::Ready(Ok(match *self.state {
                ReadNextState::Reading => {
                    let (frame_type, mut bytes, fin) =
                        ready!(self.next_frame.as_mut().unwrap().poll_unpin(ctx))?;
                    match frame_type {
                        FrameType::Hello => {
                            if fin {
                                return Poll::Ready(Err(format_err!("unexpected end of stream")));
                            }
                            if bytes.len() != 0 {
                                return Poll::Ready(Err(format_err!("Hello frame must be empty")));
                            }
                            Frame::Hello
                        }
                        FrameType::Data => {
                            if fin {
                                return Poll::Ready(Err(format_err!("unexpected end of stream")));
                            }
                            *self.state =
                                ReadNextState::DeserializingData(bytes, Msg::Parser::new());
                            continue;
                        }
                        FrameType::Control => match (fin, decode_fidl(&mut bytes)?) {
                            (true, StreamControl::AckTransfer(Empty {})) => Frame::AckTransfer,
                            (true, StreamControl::EndTransfer(Empty {})) => Frame::EndTransfer,
                            (true, StreamControl::Shutdown(status_code)) => {
                                Frame::Shutdown(zx_status::Status::ok(status_code))
                            }
                            (
                                false,
                                StreamControl::BeginTransfer(BeginTransfer {
                                    new_destination_node,
                                    transfer_key,
                                }),
                            ) => Frame::BeginTransfer(new_destination_node.into(), transfer_key),
                            (true, x) => {
                                return Poll::Ready(Err(format_err!(
                                    "Unexpected end of stream after {:?}",
                                    x
                                )))
                            }
                            (false, x) => {
                                return Poll::Ready(Err(format_err!(
                                    "Expected end of stream after {:?}",
                                    x
                                )))
                            }
                        },
                    }
                }
                ReadNextState::DeserializingData(ref mut bytes, ref mut parser) => {
                    ready!(parser.poll_ser(
                        self.incoming_message.as_mut().unwrap(),
                        bytes,
                        self.conn,
                        self.stats,
                        &mut self.router_holder,
                        ctx
                    ))?;
                    *self.state = ReadNextState::Reading;
                    Frame::Data(self.incoming_message.take().unwrap())
                }
            }));
        }
    }
}

impl<'a, Msg: Message> Future for ReadNext<'a, Msg> {
    type Output = Result<Frame<'a, Msg>, Error>;
    fn poll(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
        Pin::into_inner(self).poll_inner(ctx)
    }
}

impl<Msg: Message> StreamProperties for StreamReader<Msg> {
    fn id(&self) -> u64 {
        self.stream.id()
    }

    fn conn(&self) -> &AsyncConnection {
        self.stream.conn()
    }
}

impl<Msg: Message> StreamReader<Msg> {
    pub(crate) fn next<'a>(&'a mut self) -> ReadNext<'a, Msg> {
        ReadNext {
            conn: &self.conn,
            next_frame: match self.state {
                ReadNextState::Reading => Some(self.stream.next()),
                ReadNextState::DeserializingData(_, _) => None,
            },
            state: &mut self.state,
            incoming_message: Some(&mut self.incoming_message),
            stats: &self.stats,
            router_holder: RouterHolder::Unused(&self.router),
        }
    }

    async fn expect(&mut self, frame: Frame<'_, Msg>) -> Result<(), Error> {
        let received = self.next().await?;
        if received != frame {
            let msg = format_err!("Expected {:?} got {:?}", frame, received);
            self.stream.abandon().await;
            Err(msg)
        } else {
            Ok(())
        }
    }

    pub(crate) async fn expect_ack_transfer(mut self) -> Result<(), Error> {
        self.expect(Frame::AckTransfer).await
    }

    pub(crate) async fn expect_hello(&mut self) -> Result<(), Error> {
        self.expect(Frame::Hello).await
    }

    pub(crate) async fn expect_shutdown(
        mut self,
        result: Result<(), zx_status::Status>,
    ) -> Result<(), Error> {
        self.expect(Frame::Shutdown(result)).await
    }
}

pub(crate) trait StreamReaderBinder {
    fn bind<Msg: Message, H: Proxyable<Message = Msg>>(
        self,
        hdl: &ProxyableHandle<H>,
    ) -> StreamReader<Msg>;
}

impl StreamReaderBinder for FramedStreamReader {
    fn bind<Msg: Message, H: Proxyable<Message = Msg>>(
        self,
        hdl: &ProxyableHandle<H>,
    ) -> StreamReader<Msg> {
        StreamReader {
            conn: self.conn().clone(),
            stream: self,
            incoming_message: Default::default(),
            router: hdl.router().clone(),
            stats: hdl.stats().clone(),
            state: ReadNextState::Reading,
        }
    }
}
