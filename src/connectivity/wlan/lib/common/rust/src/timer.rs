// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{channel::mpsc, FutureExt, Stream, StreamExt};

use crate::sink::UnboundedSink;

// TODO(fxbug.dev/85248): Change these names to something less visually similar.
pub type TimeEntry<E> = (zx::Time, TimedEvent<E>);
pub type TimeSender<E> = UnboundedSink<TimeEntry<E>>;
pub type TimeStream<E> = mpsc::UnboundedReceiver<TimeEntry<E>>;
pub type EventId = u64;

// The returned timer will send scheduled timeouts to the returned TimeStream.
// Note that this will not actually have any timed behavior unless events are pulled off
// the TimeStream and handled asynchronously.
pub fn create_timer<E>() -> (Timer<E>, TimeStream<E>) {
    let (timer_sink, time_stream) = mpsc::unbounded();
    (Timer::new(UnboundedSink::new(timer_sink)), time_stream)
}

pub fn make_async_timed_event_stream<E>(
    time_stream: impl Stream<Item = TimeEntry<E>>,
) -> impl Stream<Item = TimedEvent<E>> {
    time_stream
        .map(|(deadline, timed_event)| {
            fasync::Timer::new(fasync::Time::from_zx(deadline)).map(|_| timed_event)
        })
        .buffer_unordered(usize::max_value())
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

#[derive(Debug)]
pub struct Timer<E> {
    sender: TimeSender<E>,
    next_id: EventId,
}

impl<E> Timer<E> {
    pub fn new(sender: TimeSender<E>) -> Self {
        Timer { sender, next_id: 0 }
    }

    pub fn now(&self) -> zx::Time {
        // We use fasync to support time manipulation in tests.
        fasync::Time::now().into_zx()
    }

    pub fn schedule_at(&mut self, deadline: zx::Time, event: E) -> EventId {
        let id = self.next_id;
        self.sender.send((deadline, TimedEvent { id, event }));
        self.next_id += 1;
        id
    }

    pub fn schedule_after(&mut self, duration: zx::Duration, event: E) -> EventId {
        self.schedule_at(fasync::Time::after(duration).into_zx(), event)
    }

    pub fn schedule<EV>(&mut self, event: EV) -> EventId
    where
        EV: TimeoutDuration + Into<E>,
    {
        self.schedule_after(event.timeout_duration(), event.into())
    }
}

pub trait TimeoutDuration {
    fn timeout_duration(&self) -> zx::Duration;
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::assert_variant,
        fuchsia_async as fasync,
        fuchsia_zircon::{self as zx, DurationNum},
        futures::channel::mpsc::{self, UnboundedSender},
        pin_utils::pin_mut,
        std::task::Poll,
    };

    type Event = u32;
    impl TimeoutDuration for Event {
        fn timeout_duration(&self) -> zx::Duration {
            10.seconds()
        }
    }

    #[test]
    fn test_timer_schedule_at() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");
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
    fn test_timer_schedule_after() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (mut timer, mut time_stream) = create_timer::<Event>();
        let timeout1 = 1000.seconds();
        let timeout2 = 5.seconds();
        assert_eq!(timer.schedule_after(timeout1, 7), 0);
        assert_eq!(timer.schedule_after(timeout2, 9), 1);

        let (t1, event1) = time_stream.try_next().unwrap().expect("expect time entry");
        assert_eq!(event1.id, 0);
        assert_eq!(event1.event, 7);

        let (t2, event2) = time_stream.try_next().unwrap().expect("expect time entry");
        assert_eq!(event2.id, 1);
        assert_eq!(event2.event, 9);

        // Confirm that the ordering of timeouts is expected. We can't check the actual
        // values since they're dependent on the system clock.
        assert!(t1.into_nanos() > t2.into_nanos());

        assert_variant!(time_stream.try_next(), Err(e) => {
            assert_eq!(e.to_string(), "receiver channel is empty")
        });
    }

    #[test]
    fn test_timer_schedule() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (mut timer, mut time_stream) = create_timer::<Event>();
        let start = zx::Time::after(0.millis());

        assert_eq!(timer.schedule(5u32), 0);

        let (t, event) = time_stream.try_next().unwrap().expect("expect time entry");
        assert_eq!(event.id, 0);
        assert_eq!(event.event, 5);
        assert!(start + 10.seconds() <= t);
    }

    #[test]
    fn test_timer_stream() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let fut = async {
            let (timer, time_stream) = mpsc::unbounded::<TimeEntry<Event>>();
            let mut timeout_stream = make_async_timed_event_stream(time_stream);
            let now = zx::Time::get_monotonic();
            schedule(&timer, now + 40.millis(), 0);
            schedule(&timer, now + 10.millis(), 1);
            schedule(&timer, now + 20.millis(), 2);
            schedule(&timer, now + 30.millis(), 3);

            let mut events = vec![];
            for _ in 0u32..4 {
                let event = timeout_stream.next().await.expect("timer terminated prematurely");
                events.push(event.event);
            }
            events
        };
        pin_mut!(fut);
        for _ in 0u32..4 {
            assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
            assert!(exec.wake_next_timer().is_some());
        }
        assert_variant!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(events) => assert_eq!(events, vec![1, 2, 3, 0]),
        );
    }

    fn schedule(timer: &UnboundedSender<TimeEntry<Event>>, deadline: zx::Time, event: Event) {
        let entry = (deadline, TimedEvent { id: 0, event });
        timer.unbounded_send(entry).expect("expect send successful");
    }
}
