// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Support for creating futures that represent timers.
//!
//! This module contains the `Timer` type which is a future that will resolve
//! at a particular point in the future.

use std::marker::PhantomData;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};

use futures::{Future, Stream, Poll, Async, Never};
use futures::task::{self, AtomicWaker};

use executor::EHandle;

use zx;

/// A trait which allows futures to be easily wrapped in a timeout.
pub trait TimeoutExt: Future + Sized {
    /// Wraps the future in a timeout, calling `on_timeout` to produce a result
    /// when the timeout occurs.
    fn on_timeout<OT>(self, time: zx::Time, on_timeout: OT)
        -> Result<OnTimeout<Self, OT>, zx::Status>
        where OT: FnOnce() -> Result<Self::Item, Self::Error>
    {
        Ok(OnTimeout {
            timer: Timer::new(time),
            future: self,
            on_timeout: Some(on_timeout),
        })
    }
}

impl<F: Future + Sized> TimeoutExt for F {}

/// A wrapper for a future which will complete with a provided closure when a timeout occurs.
#[derive(Debug)]
#[must_use = "futures do nothing unless polled"]
pub struct OnTimeout<F: Future, OT> {
    timer: Timer<Never>,
    future: F,
    on_timeout: Option<OT>,
}

impl<F: Future, OT> Future for OnTimeout<F, OT>
    where OT: FnOnce() -> Result<F::Item, F::Error>
{
    type Item = F::Item;
    type Error = F::Error;
    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        if let Async::Ready(item) = self.future.poll(cx)? {
            return Ok(Async::Ready(item));
        }
        if let Async::Ready(()) = self.timer.poll(cx).map_err(|never| match never {})? {
            let ot = self.on_timeout.take().expect("polled withtimeout after completion");
            let item = (ot)()?;
            return Ok(Async::Ready(item));
        }
        Ok(Async::Pending)
    }
}

/// An asynchronous timer.
#[derive(Debug)]
#[must_use = "futures do nothing unless polled"]
pub struct Timer<E> {
    waker_and_bool: Arc<(AtomicWaker, AtomicBool)>,
    error_marker: PhantomData<E>,
}

impl<E> Timer<E> {
    /// Create a new timer scheduled to fire at `time`.
    pub fn new(time: zx::Time) -> Self {
        let waker_and_bool = Arc::new((AtomicWaker::new(), AtomicBool::new(false)));
        EHandle::local().register_timer(time, &waker_and_bool);
        Timer { waker_and_bool, error_marker: PhantomData }
    }

    /// Reset the `Timer` to a fire at a new time.
    /// The `Timer` must have already fired since last being reset.
    pub fn reset(&mut self, time: zx::Time) {
        assert!(self.did_fire());
        self.waker_and_bool.1.store(false, Ordering::SeqCst);
        EHandle::local().register_timer(time, &self.waker_and_bool)
    }

    fn did_fire(&self) -> bool {
        self.waker_and_bool.1.load(Ordering::SeqCst)
    }

    fn register_task(&self, cx: &mut task::Context) {
        self.waker_and_bool.0.register(cx.waker());
    }
}

impl<E> Future for Timer<E> {
    type Item = ();
    type Error = E;
    fn poll(&mut self, cx: &mut task::Context) -> Poll<(), E> {
        if self.did_fire() {
            Ok(Async::Ready(()))
        } else {
            self.register_task(cx);
            Ok(Async::Pending)
        }
    }
}

/// An asynchronous interval timer.
/// This is a stream of events resolving at a rate of once-per interval.
#[derive(Debug)]
#[must_use = "streams do nothing unless polled"]
pub struct Interval<E> {
    timer: Timer<Never>,
    next: zx::Time,
    duration: zx::Duration,
    error_marker: PhantomData<E>,
}

impl<E> Interval<E> {
    /// Create a new `Interval` which yields every `duration`.
    pub fn new(duration: zx::Duration) -> Self {
        let next = duration.after_now();
        Interval {
            timer: Timer::new(next),
            next,
            duration,
            error_marker: PhantomData,
        }
    }
}

impl<E> Stream for Interval<E> {
    type Item = ();
    type Error = E;
    fn poll_next(&mut self, cx: &mut task::Context) -> Poll<Option<()>, Self::Error> {
        match self.timer.poll(cx) {
            Ok(Async::Ready(())) => {
                self.timer.register_task(cx);
                self.next += self.duration;
                self.timer.reset(self.next);
                Ok(Async::Ready(Some(())))
            }
            Ok(Async::Pending) => {
                self.timer.register_task(cx);
                Ok(Async::Pending)
            }
            Err(never) => match never {},
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use {Executor, Timer};
    use futures::prelude::*;
    use futures::future::Either;
    use zx::prelude::*;

    #[test]
    fn shorter_fires_first() {
        let mut exec = Executor::new().unwrap();
        let shorter = Timer::<Never>::new(100.millis().after_now());
        let longer = Timer::<Never>::new(1.second().after_now());
        match exec.run_singlethreaded(shorter.select(longer)).unwrap() {
            Either::Left(_) => {},
            Either::Right(_) => panic!("wrong timer fired"),
        }
    }

    #[test]
    fn shorter_fires_first_multithreaded() {
        let mut exec = Executor::new().unwrap();
        let shorter = Timer::<Never>::new(100.millis().after_now());
        let longer = Timer::<Never>::new(1.second().after_now());
        match exec.run(shorter.select(longer), 4).unwrap() {
            Either::Left(_) => {},
            Either::Right(_) => panic!("wrong timer fired"),
        }
    }

    #[test]
    fn fires_after_timeout() {
        let mut exec = Executor::new().unwrap();
        let deadline = 5.seconds().after_now();
        let mut future = Timer::<Never>::new(deadline);
        assert_eq!(Ok(Async::Pending), exec.run_until_stalled(&mut future));
        assert_eq!(Some(deadline), exec.wake_next_timer());
        assert_eq!(Ok(Async::Ready(())), exec.run_until_stalled(&mut future));
    }

    #[test]
    fn interval() {
        let mut exec = Executor::new().unwrap();
        let start = 0.seconds().after_now();

        let counter = Arc::new(::std::sync::atomic::AtomicUsize::new(0));
        let mut future = {
            let counter = counter.clone();
            Interval::<Never>::new(5.seconds())
                .for_each(move |()| {
                    counter.fetch_add(1, Ordering::SeqCst);
                    Ok(())
                })
                .map(|_stream| ())
        };

        // Poll for the first time before the timer runs
        assert_eq!(Ok(Async::Pending), exec.run_until_stalled(&mut future));
        assert_eq!(0, counter.load(Ordering::SeqCst));

        // Pretend to wait until the next timer
        let first_deadline = exec.wake_next_timer().expect("Expected a pending timeout (1)");
        assert!(first_deadline >= start + 5.seconds());
        assert_eq!(Ok(Async::Pending), exec.run_until_stalled(&mut future));
        assert_eq!(1, counter.load(Ordering::SeqCst));

        // Polling again before the timer runs shouldn't produce another item from the stream
        assert_eq!(Ok(Async::Pending), exec.run_until_stalled(&mut future));
        assert_eq!(1, counter.load(Ordering::SeqCst));

        // "Wait" until the next timeout and poll again: expect another item from the stream
        let second_deadline = exec.wake_next_timer().expect("Expected a pending timeout (2)");
        assert_eq!(Ok(Async::Pending), exec.run_until_stalled(&mut future));
        assert_eq!(2, counter.load(Ordering::SeqCst));

        assert_eq!(second_deadline, first_deadline + 5.seconds());
    }

}
