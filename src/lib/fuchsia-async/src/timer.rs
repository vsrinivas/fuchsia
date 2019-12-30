// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Support for creating futures that represent timers.
//!
//! This module contains the `Timer` type which is a future that will resolve
//! at a particular point in the future.

use {
    crate::executor::{EHandle, Time},
    fuchsia_zircon as zx,
    futures::{
        stream::FusedStream,
        task::{AtomicWaker, Context},
        FutureExt, Stream,
    },
    pin_utils::{unsafe_pinned, unsafe_unpinned},
    std::{
        future::Future,
        marker::Unpin,
        pin::Pin,
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc,
        },
        task::Poll,
    },
};

/// A trait which allows futures to be easily wrapped in a timeout.
pub trait TimeoutExt: Future + Sized {
    /// Wraps the future in a timeout, calling `on_timeout` to produce a result
    /// when the timeout occurs.
    fn on_timeout<OT>(self, time: Time, on_timeout: OT) -> OnTimeout<Self, OT>
    where
        OT: FnOnce() -> Self::Output,
    {
        OnTimeout { timer: Timer::new(time), future: self, on_timeout: Some(on_timeout) }
    }
}

impl<F: Future + Sized> TimeoutExt for F {}

/// A wrapper for a future which will complete with a provided closure when a timeout occurs.
#[derive(Debug)]
#[must_use = "futures do nothing unless polled"]
pub struct OnTimeout<F, OT> {
    timer: Timer,
    future: F,
    on_timeout: Option<OT>,
}

impl<F, OT> OnTimeout<F, OT> {
    // Safety: this is safe because `OnTimeout` is only `Unpin` if
    // the future is `Unpin`, and aside from `future`, all other fields are
    // treated as movable.
    unsafe_unpinned!(timer: Timer);
    unsafe_pinned!(future: F);
    unsafe_unpinned!(on_timeout: Option<OT>);
}

impl<F: Unpin, OT> Unpin for OnTimeout<F, OT> {}

impl<F: Future, OT> Future for OnTimeout<F, OT>
where
    OT: FnOnce() -> F::Output,
{
    type Output = F::Output;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        if let Poll::Ready(item) = self.as_mut().future().poll(cx) {
            return Poll::Ready(item);
        }
        if let Poll::Ready(()) = self.as_mut().timer().poll_unpin(cx) {
            let ot = OnTimeout::on_timeout(self.as_mut())
                .take()
                .expect("polled withtimeout after completion");
            let item = (ot)();
            return Poll::Ready(item);
        }
        Poll::Pending
    }
}

/// An asynchronous timer.
#[derive(Debug)]
#[must_use = "futures do nothing unless polled"]
pub struct Timer {
    waker_and_bool: Arc<(AtomicWaker, AtomicBool)>,
}

impl Unpin for Timer {}

impl Timer {
    /// Create a new timer scheduled to fire at `time`.
    pub fn new(time: Time) -> Self {
        let waker_and_bool = Arc::new((AtomicWaker::new(), AtomicBool::new(false)));
        EHandle::local().register_timer(time, &waker_and_bool);
        Timer { waker_and_bool }
    }

    /// Reset the `Timer` to a fire at a new time.
    /// The `Timer` must have already fired since last being reset.
    pub fn reset(&mut self, time: Time) {
        assert!(self.did_fire());
        self.waker_and_bool.1.store(false, Ordering::SeqCst);
        EHandle::local().register_timer(time, &self.waker_and_bool)
    }

    fn did_fire(&self) -> bool {
        self.waker_and_bool.1.load(Ordering::SeqCst)
    }

    fn register_task(&self, cx: &mut Context<'_>) {
        self.waker_and_bool.0.register(cx.waker());
    }
}

impl Future for Timer {
    type Output = ();
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        if self.did_fire() {
            Poll::Ready(())
        } else {
            self.register_task(cx);
            Poll::Pending
        }
    }
}

/// An asynchronous interval timer.
/// This is a stream of events resolving at a rate of once-per interval.
#[derive(Debug)]
#[must_use = "streams do nothing unless polled"]
pub struct Interval {
    timer: Timer,
    next: Time,
    duration: zx::Duration,
}

impl Interval {
    /// Create a new `Interval` which yields every `duration`.
    pub fn new(duration: zx::Duration) -> Self {
        let next = Time::after(duration);
        Interval { timer: Timer::new(next), next, duration }
    }
}

impl Unpin for Interval {}

impl FusedStream for Interval {
    fn is_terminated(&self) -> bool {
        // `Interval` never yields `None`
        false
    }
}

impl Stream for Interval {
    type Item = ();
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = &mut *self;
        match this.timer.poll_unpin(cx) {
            Poll::Ready(()) => {
                this.timer.register_task(cx);
                this.next += this.duration;
                this.timer.reset(this.next);
                Poll::Ready(Some(()))
            }
            Poll::Pending => {
                this.timer.register_task(cx);
                Poll::Pending
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{
        temp::{Either, TempFutureExt},
        Executor, Timer,
    };
    use fuchsia_zircon::prelude::*;
    use futures::prelude::*;

    #[test]
    fn shorter_fires_first() {
        let mut exec = Executor::new().unwrap();
        let shorter = Timer::new(Time::after(100.millis()));
        let longer = Timer::new(Time::after(1.second()));
        match exec.run_singlethreaded(shorter.select(longer)) {
            Either::Left(()) => {}
            Either::Right(()) => panic!("wrong timer fired"),
        }
    }

    #[test]
    fn shorter_fires_first_multithreaded() {
        let mut exec = Executor::new().unwrap();
        let shorter = Timer::new(Time::after(100.millis()));
        let longer = Timer::new(Time::after(1.second()));
        match exec.run(shorter.select(longer), 4) {
            Either::Left(()) => {}
            Either::Right(()) => panic!("wrong timer fired"),
        }
    }

    #[test]
    fn fires_after_timeout() {
        let mut exec = Executor::new().unwrap();
        let deadline = Time::after(5.seconds());
        let mut future = Timer::new(deadline);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut future));
        assert_eq!(Some(deadline), exec.wake_next_timer());
        assert_eq!(Poll::Ready(()), exec.run_until_stalled(&mut future));
    }

    #[test]
    fn interval() {
        let mut exec = Executor::new().unwrap();
        let start = Time::now();

        let counter = Arc::new(::std::sync::atomic::AtomicUsize::new(0));
        let mut future = {
            let counter = counter.clone();
            Interval::new(5.seconds())
                .map(move |()| {
                    counter.fetch_add(1, Ordering::SeqCst);
                })
                .collect::<()>()
        };

        // PollResult for the first time before the timer runs
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut future));
        assert_eq!(0, counter.load(Ordering::SeqCst));

        // Pretend to wait until the next timer
        let first_deadline = exec.wake_next_timer().expect("Expected a pending timeout (1)");
        assert!(first_deadline >= start + 5.seconds());
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut future));
        assert_eq!(1, counter.load(Ordering::SeqCst));

        // PollResulting again before the timer runs shouldn't produce another item from the stream
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut future));
        assert_eq!(1, counter.load(Ordering::SeqCst));

        // "Wait" until the next timeout and poll again: expect another item from the stream
        let second_deadline = exec.wake_next_timer().expect("Expected a pending timeout (2)");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut future));
        assert_eq!(2, counter.load(Ordering::SeqCst));

        assert_eq!(second_deadline, first_deadline + 5.seconds());
    }

    #[test]
    fn timer_fake_time() {
        let mut exec = Executor::new_with_fake_time().unwrap();
        exec.set_fake_time(Time::from_nanos(0));

        let mut timer = Timer::new(Time::after(1.seconds()));
        assert_eq!(exec.wake_expired_timers(), false);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut timer));

        exec.set_fake_time(Time::after(1.seconds()));
        assert_eq!(exec.wake_expired_timers(), true);
        assert_eq!(Poll::Ready(()), exec.run_until_stalled(&mut timer));
    }
}
