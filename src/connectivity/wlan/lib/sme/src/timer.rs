// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

impl<E: Clone> Clone for TimedEvent<E> {
    fn clone(&self) -> Self {
        TimedEvent { id: self.id, event: self.event.clone() }
    }
}

pub(crate) struct Timer<E> {
    sender: TimeSender<E>,
    counter: EventId,
}

impl<E> Timer<E> {
    pub fn new(sender: TimeSender<E>) -> Self {
        Timer { sender, counter: 0 }
    }

    pub fn schedule_at(&mut self, deadline: zx::Time, event: E) -> EventId {
        let id = self.counter;
        self.sender.send((deadline, TimedEvent { id, event }));
        self.counter += 1;
        id
    }

    pub fn schedule<EV>(&mut self, event: EV) -> EventId
    where
        EV: TimeoutDuration + Into<E>,
    {
        self.schedule_at(zx::Time::after(event.timeout_duration()), event.into())
    }
}

impl<E: fmt::Debug> fmt::Debug for Timer<E> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Timer").field("sender", &self.sender).finish()
    }
}

pub trait TimeoutDuration {
    fn timeout_duration(&self) -> zx::Duration;
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_zircon::prelude::DurationNum;
    use wlan_common::assert_variant;

    type Event = u32;
    impl TimeoutDuration for Event {
        fn timeout_duration(&self) -> zx::Duration {
            10.seconds()
        }
    }

    #[test]
    fn test_timer_schedule_at() {
        let (mut timer, mut time_stream) = create_timer::<Event>();
        let timeout1 = zx::Time::after(5.seconds());
        let timeout2 = zx::Time::after(10.seconds());
        assert_eq!(timer.schedule_at(timeout1, 7), 0);
        assert_eq!(timer.schedule_at(timeout2, 9), 1);

        let (t1, event1) = time_stream.try_next().unwrap().expect("expect time entry");
        assert_eq!(t1, timeout1);
        assert_eq!(event1.id, 0);
        assert_eq!(event1.event, 7);

        let (t2, event2) = time_stream.try_next().unwrap().expect("expect time entry");
        assert_eq!(t2, timeout2);
        assert_eq!(event2.id, 1);
        assert_eq!(event2.event, 9);

        assert_variant!(time_stream.try_next(), Err(e) => {
            assert_eq!(e.to_string(), "receiver channel is empty")
        });
    }

    #[test]
    fn test_timer_schedule() {
        let (mut timer, mut time_stream) = create_timer::<Event>();
        let start = zx::Time::after(0.millis());

        assert_eq!(timer.schedule(5u32), 0);

        let (t, event) = time_stream.try_next().unwrap().expect("expect time entry");
        assert_eq!(event.id, 0);
        assert_eq!(event.event, 5);
        assert!(start + 10.seconds() <= t);
    }
}
