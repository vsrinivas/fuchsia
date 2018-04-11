// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(fnbox)]

#[macro_use] 
extern crate futures;
#[macro_use]
extern crate failure;
extern crate bytes;
extern crate flatten_future_sink;
extern crate mesh_protocol;
extern crate mesh_router;

mod chunk;
mod stream_type;

use bytes::Bytes;
use chunk::LinearizeChunks;
use failure::Error;
use futures::{future::FutureResult, task::Context, Async, Future, FutureExt, Sink};
use mesh_router::StreamDataAccessor;
use std::{cell::RefCell, rc::Rc};

const REASSEMBLY_WINDOW: u64 = 1024 * 1024;

trait EndpointStreamHolder: mesh_router::StreamDataHolder {
    fn endpoint_stream(&self) -> &EndpointStream;
    fn endpoint_stream_mut(&mut self) -> &mut EndpointStream;
}

trait EndpointHandler {
    type StreamHolder: EndpointStreamHolder;

    type MessageSink: Sink<SinkItem = Bytes, SinkError = Error>;
    fn stream_recv(&mut self, stream: Rc<RefCell<Self::StreamHolder>>) -> Self::MessageSink;

    fn new_stream(&mut self, es: EndpointStream, arg: &[u8]) -> Rc<RefCell<Self::StreamHolder>>;
}

struct EndpointStream {
    stream: mesh_router::StreamData,
    stream_type: stream_type::StreamType,
}

struct Endpoint<Handler> {
    handler: Rc<RefCell<Handler>>,
}

impl<Handler> mesh_router::NodeHandler for Endpoint<Handler>
where
    Handler: EndpointHandler,
{
    // TODO(cramertj): it's very important that no allocation occurs here (how can we write this
    // type without Box)
    type Msg = chunk::Linearizer<flatten_future_sink::FlattenSink<ReadyToReceive<Handler>>>;
    type MessageStream = Handler::StreamHolder;
    type IntroFuture = FutureResult<Rc<RefCell<Self::MessageStream>>, Error>;
    type ForkedFuture = FutureResult<Rc<RefCell<Self::MessageStream>>, Error>;

    fn new_stream(
        &mut self, new_stream_data: mesh_router::StreamData, arg: &[u8],
    ) -> Rc<RefCell<Self::MessageStream>> {
        self.handler.borrow_mut().new_stream(
            EndpointStream {
                stream_type: stream_type::StreamType::new(new_stream_data.id().stream_type()),
                stream: new_stream_data,
            },
            arg,
        )
    }

    fn intro(
        &mut self, new_stream_data: mesh_router::StreamData, arg: Vec<u8>,
    ) -> Self::IntroFuture {
        unimplemented!()
    }

    fn stream_begin_msg(
        &mut self, stream: Rc<RefCell<Self::MessageStream>>, seq: u64, len: u64,
    ) -> Self::Msg {
        let handler = self.handler.clone();
        flatten_future_sink::new(ReadyToReceive {
            stream,
            handler: self.handler.clone(),
            seq,
        }).with_chunks(REASSEMBLY_WINDOW)
    }

    fn stream_fork(
        &mut self, stream: Rc<RefCell<Self::MessageStream>>,
        new_stream_data: mesh_router::StreamData, arg: Vec<u8>,
    ) -> Self::ForkedFuture {
        unimplemented!()
    }
}

struct ReadyToReceive<Handler>
where
    Handler: EndpointHandler,
{
    stream: Rc<RefCell<Handler::StreamHolder>>,
    handler: Rc<RefCell<Handler>>,
    seq: u64,
}

impl<Handler> Future for ReadyToReceive<Handler>
where
    Handler: EndpointHandler,
{
    type Item = Handler::MessageSink;
    type Error = Error;

    fn poll(&mut self, cx: &mut Context) -> Result<Async<Self::Item>, Self::Error> {
        let r = {
            let mut s = self.stream.borrow_mut();
            let peer = s.peer();
            let id = s.id().index();
            s.endpoint_stream_mut()
                .stream_type
                .poll_impl(cx, self.seq, peer, id)
        };
        match r {
            stream_type::PollResult::Accept => Ok(Async::Ready(
                self.handler.borrow_mut().stream_recv(self.stream.clone()),
            )),
            stream_type::PollResult::Wait { .. } => Ok(Async::Pending),
            stream_type::PollResult::Err(e) => Err(e.into()),
        }
    }
}
