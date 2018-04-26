// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate futures;

use futures::{task::Context, Async, Sink};

enum UnionSinkType<A, B> {
    TypeA(A),
    TypeB(B),
}

pub struct UnionSink<A, B> {
    t: UnionSinkType<A, B>,
}

impl<A, B, Item, Error> Sink for UnionSink<A, B>
where
    A: Sink<SinkItem = Item, SinkError = Error>,
    B: Sink<SinkItem = Item, SinkError = Error>,
{
    type SinkItem = Item;
    type SinkError = Error;

    fn poll_ready(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
        use union_sink::UnionSinkType::*;
        match &mut self.t {
            TypeA(a) => a.poll_ready(cx),
            TypeB(b) => b.poll_ready(cx),
        }
    }

    fn start_send(&mut self, item: Item) -> Result<(), Error> {
        use union_sink::UnionSinkType::*;
        match &mut self.t {
            TypeA(a) => a.start_send(item),
            TypeB(b) => b.start_send(item),
        }
    }

    fn poll_flush(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
        use union_sink::UnionSinkType::*;
        match &mut self.t {
            TypeA(a) => a.poll_flush(cx),
            TypeB(b) => b.poll_flush(cx),
        }
    }

    fn poll_close(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
        use union_sink::UnionSinkType::*;
        match &mut self.t {
            TypeA(a) => a.poll_close(cx),
            TypeB(b) => b.poll_close(cx),
        }
    }
}

impl<A, B> UnionSink<A, B> {
    pub fn from_a(a: A) -> UnionSink<A, B> {
        UnionSink {
            t: UnionSinkType::TypeA(a),
        }
    }

    pub fn from_b(b: B) -> UnionSink<A, B> {
        UnionSink {
            t: UnionSinkType::TypeB(b),
        }
    }
}
