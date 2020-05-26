// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::async_quic::{AsyncConnection, StreamProperties};
use crate::coding::{decode_fidl, encode_fidl};
use crate::framed_stream::{FrameType, FramedStreamReader, FramedStreamWriter, MessageStats};
use crate::labels::{NodeId, TransferKey};
use crate::proxyable_handle::{Message, Proxyable, ProxyableHandle, RouterHolder, Serializer};
use crate::router::Router;
use anyhow::{bail, ensure, format_err, Context as _, Error};
use fidl_fuchsia_overnet_protocol::{BeginTransfer, Empty, StreamControl};
use fuchsia_zircon_status as zx_status;
use futures::future::poll_fn;
use std::rc::{Rc, Weak};

pub(crate) struct StreamWriter<Msg: Message> {
    stream: FramedStreamWriter,
    send_buffer: Vec<u8>,
    stats: Rc<MessageStats>,
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
    stream: FramedStreamReader,
    incoming_message: Msg,
    router: Weak<Router>,
    stats: Rc<MessageStats>,
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
    pub(crate) async fn next<'a>(&'a mut self) -> Result<Frame<'a, Msg>, Error> {
        let (frame_type, mut bytes, fin) = self.stream.next().await?;
        Ok(match frame_type {
            FrameType::Hello => {
                ensure!(!fin, "unexpected end of stream");
                ensure!(bytes.len() == 0, "Hello frame must be empty");
                Frame::Hello
            }
            FrameType::Data => {
                ensure!(!fin, "unexpected end of stream");
                let mut parser = Msg::Parser::new();
                let conn = self.stream.conn();
                let incoming_message = &mut self.incoming_message;
                let mut rh = RouterHolder::Unused(&self.router);
                let stats = &self.stats;
                poll_fn(move |ctx| {
                    parser.poll_ser(incoming_message, &mut bytes, conn, stats, &mut rh, ctx)
                })
                .await?;
                Frame::Data(&mut self.incoming_message)
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
                (true, x) => bail!("Unexpected end of stream after {:?}", x),
                (false, x) => bail!("Expected end of stream after {:?}", x),
            },
        })
    }

    async fn expect(&mut self, frame: Frame<'_, Msg>) -> Result<(), Error> {
        let received = self.next().await?;
        if received != frame {
            let msg = format_err!("Expected {:?} got {:?}", frame, received);
            self.stream.abandon();
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
            stream: self,
            incoming_message: Default::default(),
            router: hdl.router().clone(),
            stats: hdl.stats().clone(),
        }
    }
}
