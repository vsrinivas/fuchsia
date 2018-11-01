// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fuchsia_zircon as zx;
use futures::channel::mpsc;
use std::fmt;

use crate::sink::UnboundedSink;

pub type TimeEntry<E> = (zx::Time, TimedEvent<E>);
pub(crate) type TimeSender<E> = UnboundedSink<TimeEntry<E>>;
pub(crate) type TimeStream<E> = mpsc::UnboundedReceiver<TimeEntry<E>>;
pub(crate) type EventId = u64;

pub(crate) fn create_timer<E>() -> (Timer<E>, TimeStream<E>) {
    let (timer_sink, time_stream) = mpsc::unbounded();
    (Timer::new(UnboundedSink::new(timer_sink)), time_stream)
}

#[derive(Debug)]
pub struct TimedEvent<E> {
    pub id: EventId,
    pub event: E,
}

pub(crate) struct Timer<E> {
    sender: TimeSender<E>,
    counter: EventId,
}

impl<E> Timer<E> {
    pub fn new(sender: TimeSender<E>) -> Self {
        Timer { sender, counter: 0 }
    }

    pub fn schedule(&mut self, deadline: zx::Time, event: E) -> EventId {
        let id = self.counter;
        self.sender.send((deadline, TimedEvent { id, event }));
        self.counter += 1;
        id
    }
}

impl<E: fmt::Debug> fmt::Debug for Timer<E> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_struct("Timer")
            .field("sender", &self.sender)
            .finish()
    }
}