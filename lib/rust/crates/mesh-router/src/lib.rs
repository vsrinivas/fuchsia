// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(fnbox)]

extern crate futures;
extern crate mesh_protocol;
#[macro_use]
extern crate failure;
extern crate bytes;
extern crate flatten_future_sink;
extern crate slab;

mod async_map;
mod union_sink;

use async_map::AsyncMap;
use bytes::Bytes;
use failure::Error;
use futures::{future,
              future::{ok, result},
              task::Context,
              Async,
              Future,
              FutureExt,
              Sink};
use std::{cell::RefCell,
          collections::{HashMap, VecDeque},
          rc::Rc};

pub struct Chunk {
    pub offset: u64,
    pub data: Bytes,
}

#[derive(Debug, Fail, Clone)]
enum RoutingError {
    #[fail(display = "No peers")]
    NoPeerLink,
    #[fail(display = "Stream closed")]
    StreamClosed,
    #[fail(
        display = "Stream type changed for stream {:?}:{} from {:?} to {:?}",
        peer,
        stream_id,
        established_type,
        received_type
    )]
    StreamTypeChanged {
        peer: mesh_protocol::NodeId,
        stream_id: u64,
        established_type: mesh_protocol::StreamType,
        received_type: mesh_protocol::StreamType,
    },
    #[fail(display = "New stream already exists for stream {:?}:{}", peer, stream_id)]
    NewStreamAlreadyExists {
        peer: mesh_protocol::NodeId,
        stream_id: u64,
    },
}

pub trait OutgoingLink {
    type Msg: Sink<SinkItem = Chunk, SinkError = Error>;

    fn begin_msg(&mut self, rh: mesh_protocol::RoutingHeader) -> Self::Msg;
}

pub trait NodeHandler {
    type Msg: Sink<SinkItem = Chunk, SinkError = Error>;

    type MessageStream: StreamDataHolder;
    type IntroFuture: Future<Item = Rc<RefCell<Self::MessageStream>>, Error = Error>;
    type ForkedFuture: Future<Item = Rc<RefCell<Self::MessageStream>>, Error = Error>;

    fn new_stream(
        &mut self, new_stream_data: StreamData, arg: &[u8],
    ) -> Rc<RefCell<Self::MessageStream>>;
    fn intro(&mut self, new_stream_data: StreamData, arg: Vec<u8>) -> Self::IntroFuture;
    fn stream_begin_msg(
        &mut self, stream: Rc<RefCell<Self::MessageStream>>, seq: u64, len: u64,
    ) -> Self::Msg;
    fn stream_fork(
        &mut self, stream: Rc<RefCell<Self::MessageStream>>, new_stream_data: StreamData,
        arg: Vec<u8>,
    ) -> Self::ForkedFuture;
}

#[derive(PartialEq, Eq, Hash, Clone, Copy)]
struct LocalStreamId {
    peer: mesh_protocol::NodeId,
    stream_id: u64,
}

pub struct Node<Handler, Outgoing>
where
    Handler: NodeHandler,
{
    id: mesh_protocol::NodeId,
    // global handler for global things
    handler: Rc<RefCell<Handler>>,
    // TODO(cramertj): if we could return a mutable ref from Lookup, then we could eliminate
    // Rc<RefCell<>> here (twice)
    outgoing: AsyncMap<mesh_protocol::NodeId, Rc<RefCell<Outgoing>>, RoutingError>,
    // TODO(cramertj): need to rc streams so that we can mutate it in forward
    streams:
        Rc<RefCell<AsyncMap<LocalStreamId, Rc<RefCell<Handler::MessageStream>>, RoutingError>>>,
    // next stream id, per peer
    next_stream_id: HashMap<mesh_protocol::NodeId, u64>,
}

pub struct StreamData {
    id: mesh_protocol::StreamId,
    peer: mesh_protocol::NodeId,
    outgoing_tip: mesh_protocol::SequenceNum,
    outgoing: VecDeque<Vec<Bytes>>,
}

pub trait StreamDataHolder {
    fn stream_data(&self) -> &StreamData;
    fn stream_data_mut(&mut self) -> &mut StreamData;
}

struct BufferingSink<S, T> {
    stream: Rc<RefCell<S>>,
    seq: u64,
    ofs: u64,
    // TODO(ctiller): remove box?
    target: T,
}

impl<S, T> Sink for BufferingSink<S, T>
where
    S: StreamDataHolder,
    T: Sink<SinkItem = Chunk, SinkError = Error>,
{
    type SinkItem = Bytes;
    type SinkError = Error;

    fn poll_ready(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
        self.target.poll_ready(cx)
    }

    fn poll_flush(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
        self.target.poll_flush(cx)
    }

    fn poll_close(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
        self.target.poll_close(cx)
    }

    fn start_send(&mut self, data: Bytes) -> Result<(), Error> {
        let mut s = self.stream.borrow_mut();
        let sd = s.stream_data_mut();
        let tip_seq = sd.outgoing_tip.index();
        if self.seq >= tip_seq {
            let idx = self.seq - tip_seq;
            println!(
                "seq={} tip={} idx={} oglen={}",
                self.seq,
                tip_seq,
                idx,
                sd.outgoing.len()
            );
            assert!(idx < sd.outgoing.len() as u64);
            assert_eq!(sd.outgoing[idx as usize].len() as u64, self.ofs);
            sd.outgoing[idx as usize].push(data.clone());
        }
        let len = data.len() as u64;
        self.target.start_send(Chunk {
            offset: self.ofs,
            data,
        })?;
        self.ofs += len;
        Ok(())
    }
}

pub trait StreamDataAccessor {
    fn id(&self) -> mesh_protocol::StreamId;
    fn peer(&self) -> mesh_protocol::NodeId;
}

impl StreamDataAccessor for StreamData {
    fn id(&self) -> mesh_protocol::StreamId {
        self.id
    }
    fn peer(&self) -> mesh_protocol::NodeId {
        self.peer
    }
}

impl<T: StreamDataHolder> StreamDataAccessor for T {
    fn id(&self) -> mesh_protocol::StreamId {
        self.stream_data().id()
    }
    fn peer(&self) -> mesh_protocol::NodeId {
        self.stream_data().peer()
    }
}

impl<Handler, Outgoing> Node<Handler, Outgoing>
where
    Handler: NodeHandler,
    Outgoing: OutgoingLink,
{
    pub fn new(id: mesh_protocol::NodeId, handler: Rc<RefCell<Handler>>) -> Self {
        Node {
            id,
            handler,
            outgoing: AsyncMap::new(),
            streams: Rc::new(RefCell::new(AsyncMap::new())),
            next_stream_id: HashMap::new(),
        }
    }

    pub fn id(&self) -> mesh_protocol::NodeId {
        self.id
    }

    pub fn add_outgoing(&mut self, peer: mesh_protocol::NodeId, out: Rc<RefCell<Outgoing>>) {
        self.outgoing.put_ok(peer, out);
    }

    fn add_new_stream<F>(
        &mut self, future_incoming: F,
    ) -> impl Future<Item = Rc<RefCell<Handler::MessageStream>>, Error = Error>
    where
        F: Future<Item = Rc<RefCell<Handler::MessageStream>>, Error = Error>,
    {
        // TODO(cramertj): eliminate need to clone streams
        let streams_clone = self.streams.clone();
        future_incoming.and_then(move |s| {
            // TODO(cramertj): streams is rc because of this: adding to streams from
            // a future
            let (stream_id, peer) = {
                let sb = s.borrow();
                let sd = sb.stream_data();
                (sd.id.index(), sd.peer)
            };
            let r = streams_clone
                .borrow_mut()
                .put_new_ok(LocalStreamId { peer, stream_id }, s.clone(), || {
                    RoutingError::NewStreamAlreadyExists { peer, stream_id }.into()
                })
                .map(|_| s);
            result(r)
        })
    }

    pub fn new_stream(
        &mut self, peer: mesh_protocol::NodeId, stream_type: mesh_protocol::StreamType,
        intro: Vec<u8>,
    ) -> impl Future<Item = Rc<RefCell<Handler::MessageStream>>, Error = Error> {
        let id = {
            let next_id: &mut u64 = self.next_stream_id
                .entry(peer)
                .or_insert(if peer > self.id { 2 } else { 1 });
            let id = *next_id;
            *next_id += 2;
            id
        };

        let s = self.handler.borrow_mut().new_stream(
            StreamData {
                id: mesh_protocol::StreamId::new(id, stream_type),
                peer,
                // TODO(ctiller): remove copy of intro
                outgoing_tip: mesh_protocol::SequenceNum::AcceptIntro(intro.clone()),
                outgoing: VecDeque::new(),
            },
            intro.as_slice(),
        );

        self.add_new_stream(ok(s))
    }

    pub fn stream_begin_send(
        &mut self, stream: Rc<RefCell<Handler::MessageStream>>, private_len: u64,
    ) -> impl Sink<SinkItem = Bytes, SinkError = Error> {
        let rh = {
            let mut s = stream.borrow_mut();
            let sd = s.stream_data_mut();
            let rh = mesh_protocol::RoutingHeader {
                dst: sd.peer,
                src: self.id,
                stream: sd.id,
                private_len,
                seq: sd.outgoing_tip.inc(sd.outgoing.len() as u64),
            };
            sd.outgoing.push_back(Vec::new());
            rh
        };
        let seq = rh.seq.index();
        BufferingSink {
            stream,
            seq,
            ofs: 0,
            target: self.forward(rh),
        }
    }

    pub fn forward(
        &mut self, rh: mesh_protocol::RoutingHeader,
    ) -> impl Sink<SinkItem = Chunk, SinkError = Error> {
        let fut = if rh.dst == self.id {
            let rh_stream = rh.stream;
            let rh_src = rh.src;
            let fut = match &rh.seq {
                mesh_protocol::SequenceNum::Num(_) => self.streams
                    .borrow_mut()
                    .lookup(LocalStreamId {
                        peer: rh_src,
                        stream_id: rh.stream.index(),
                    })
                    .and_then(move |s| {
                        result(if s.borrow().stream_data().id.stream_type()
                            != rh_stream.stream_type()
                        {
                            Err(RoutingError::StreamTypeChanged {
                                peer: rh_src,
                                stream_id: rh_stream.index(),
                                established_type: s.borrow().stream_data().id.stream_type(),
                                received_type: rh_stream.stream_type(),
                            }.into())
                        } else {
                            Ok(s)
                        })
                    })
                    .left(),
                // TODO(cramertj): Right() here is needed because future::Either defines left,
                // right... defeating .left().right() pattern
                mesh_protocol::SequenceNum::Fork(src, arg) => future::Either::Right({
                    // TODO(ctiller): eliminate copy of arg
                    let arg_clone = arg.clone();
                    let handler_clone = self.handler.clone();
                    let incoming_fut = self.streams
                        .borrow_mut()
                        .lookup(LocalStreamId {
                            peer: rh_src,
                            stream_id: *src,
                        })
                        .and_then(move |s| {
                            let result = handler_clone.borrow_mut().stream_fork(
                                s,
                                StreamData {
                                    id: rh_stream,
                                    peer: rh_src,
                                    outgoing_tip: 1.into(),
                                    outgoing: VecDeque::new(),
                                },
                                arg_clone,
                            );
                            result
                        });
                    self.add_new_stream(incoming_fut).left()
                }),
                mesh_protocol::SequenceNum::AcceptIntro(arg) => future::Either::Right({
                    let incoming_fut = self.handler.borrow_mut()
                        // TODO(ctiller): eliminate copy
                        .intro(StreamData {
                            id: rh_stream,
                            peer: rh_src,
                            outgoing_tip: 1.into(),
                            outgoing: VecDeque::new(),
                        }, arg.clone());
                    self.add_new_stream(incoming_fut).right()
                }),
                // TODO(cramertj): add an Either impl to Sink and remove UnionSink
            };
            let handler_clone = self.handler.clone();
            fut.map(move |s| {
                let result = union_sink::UnionSink::from_a(
                    handler_clone
                        .borrow_mut()
                        .stream_begin_msg(s, rh.seq.index(), rh.private_len),
                );
                result
            }).left()
        } else {
            self.outgoing
                .lookup(rh.dst)
                // TODO(cramertj): add an Either impl to Sink and remove UnionSink
                .map(|out| union_sink::UnionSink::from_b(out.borrow_mut().begin_msg(rh)))
                .right()
        };
        flatten_future_sink::new(fut)
    }
}
