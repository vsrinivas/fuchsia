// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{
    signals::Collector, IntoProxied, Message, Proxyable, ProxyableRW, ReadValue, RouterHolder,
    Serializer, IO,
};
use crate::coding;
use crate::peer::{MessageStats, PeerConnRef};
use anyhow::{format_err, Error};
use fidl::{AsHandleRef, HandleBased, Peered, Signals};
use fuchsia_zircon_status as zx_status;
use std::sync::Arc;
use std::task::{Context, Poll};

pub(crate) struct EventPair {
    event_pair: fidl::EventPair,
}

impl std::fmt::Debug for EventPair {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.event_pair.fmt(f)
    }
}

impl Proxyable for EventPair {
    type Message = EventPairMessage;

    fn from_fidl_handle(hdl: fidl::Handle) -> Result<Self, Error> {
        Ok(fidl::EventPair::from_handle(hdl).into_proxied()?)
    }

    fn into_fidl_handle(self) -> Result<fidl::Handle, Error> {
        Ok(self.event_pair.into_handle())
    }

    fn signal_peer(&self, clear: Signals, set: Signals) -> Result<(), Error> {
        self.event_pair.signal_peer(clear, set).map_err(Into::into)
    }
}

impl<'a> ProxyableRW<'a> for EventPair {
    type Reader = EventPairReader<'a>;
    type Writer = EventPairWriter;
}

impl IntoProxied for fidl::EventPair {
    type Proxied = EventPair;
    fn into_proxied(self) -> Result<EventPair, Error> {
        Ok(EventPair { event_pair: self })
    }
}

#[derive(Debug, Default, PartialEq)]
pub(crate) struct EventPairMessage;

impl Message for EventPairMessage {
    type Parser = EventPairParser;
    type Serializer = EventPairSerializer;
}

pub(crate) struct EventPairReader<'a> {
    collector: Collector<'a>,
}

impl<'a> IO<'a> for EventPairReader<'a> {
    type Proxyable = EventPair;
    type Output = ReadValue;
    fn new() -> Self {
        EventPairReader { collector: Default::default() }
    }
    fn poll_io(
        &mut self,
        _: &mut EventPairMessage,
        event_pair: &'a EventPair,
        fut_ctx: &mut Context<'_>,
    ) -> Poll<Result<ReadValue, zx_status::Status>> {
        // There's no such thing as an event pair message, so we just pretend to be waiting forever
        let read_result = Poll::Pending;
        self.collector.after_read(fut_ctx, event_pair.event_pair.as_handle_ref(), read_result, true)
    }
}

pub(crate) struct EventPairWriter;

impl IO<'_> for EventPairWriter {
    type Proxyable = EventPair;
    type Output = ();
    fn new() -> Self {
        EventPairWriter
    }
    fn poll_io(
        &mut self,
        _: &mut EventPairMessage,
        _: &EventPair,
        _: &mut Context<'_>,
    ) -> Poll<Result<(), zx_status::Status>> {
        // We always fail to parse event pair messages, therefore we can never write one to an object
        unreachable!()
    }
}

#[derive(Debug)]
pub(crate) struct EventPairSerializer;

impl Serializer for EventPairSerializer {
    type Message = EventPairMessage;
    fn new() -> EventPairSerializer {
        EventPairSerializer
    }
    fn poll_ser(
        &mut self,
        _: &mut Self::Message,
        _: &mut Vec<u8>,
        _: PeerConnRef<'_>,
        _: &Arc<MessageStats>,
        _: &mut RouterHolder<'_>,
        _: &mut Context<'_>,
        _: coding::Context,
    ) -> Poll<Result<(), Error>> {
        // Reading from the event pair is always pending, therefore we can never serialize a message
        unreachable!()
    }
}

#[derive(Debug)]
pub(crate) struct EventPairParser;

impl Serializer for EventPairParser {
    type Message = EventPairMessage;
    fn new() -> EventPairParser {
        EventPairParser
    }
    fn poll_ser(
        &mut self,
        _: &mut Self::Message,
        _: &mut Vec<u8>,
        _: PeerConnRef<'_>,
        _: &Arc<MessageStats>,
        _: &mut RouterHolder<'_>,
        _: &mut Context<'_>,
        _: coding::Context,
    ) -> Poll<Result<(), Error>> {
        Poll::Ready(Err(format_err!("Event pairs do not exchange message payloads")))
    }
}
