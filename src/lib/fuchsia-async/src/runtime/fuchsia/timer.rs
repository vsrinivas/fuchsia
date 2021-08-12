// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Support for creating futures that represent timers.
//!
//! This module contains the `Timer` type which is a future that will resolve
//! at a particular point in the future.

use crate::runtime::{EHandle, Time, WakeupTime};
use fuchsia_zircon as zx;
use futures::{
    stream::FusedStream,
    task::{AtomicWaker, Context},
    FutureExt, Stream,
};
use std::{
    cmp,
    collections::BinaryHeap,
    future::Future,
    marker::Unpin,
    pin::Pin,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Weak,
    },
    task::Poll,
};

impl WakeupTime for std::time::Instant {
    fn into_time(self) -> Time {
        let now_as_instant = std::time::Instant::now();
        let now_as_time = Time::now();
        now_as_time + self.saturating_duration_since(now_as_instant).into()
    }
}

impl WakeupTime for Time {
    fn into_time(self) -> Time {
        self
    }
}

impl WakeupTime for zx::Time {
    fn into_time(self) -> Time {
        self.into()
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
    pub fn new<WT>(time: WT) -> Self
    where
        WT: WakeupTime,
    {
        let waker_and_bool = Arc::new((AtomicWaker::new(), AtomicBool::new(false)));
        let this = Timer { waker_and_bool };
        EHandle::register_timer(time.into_time(), this.handle());
        this
    }

    fn handle(&self) -> TimerHandle {
        TimerHandle { inner: Arc::downgrade(&self.waker_and_bool) }
    }

    /// Reset the `Timer` to a fire at a new time.
    /// The `Timer` must have already fired since last being reset.
    pub fn reset(&mut self, time: Time) {
        assert!(self.did_fire());
        self.waker_and_bool.1.store(false, Ordering::SeqCst);
        EHandle::register_timer(time, self.handle());
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
        // See https://docs.rs/futures/0.3.5/futures/task/struct.AtomicWaker.html
        // for more information.
        // quick check to avoid registration if already done.
        if self.did_fire() {
            return Poll::Ready(());
        }

        self.register_task(cx);

        // Need to check condition **after** `register` to avoid a race
        // condition that would result in lost notifications.
        if self.did_fire() {
            Poll::Ready(())
        } else {
            Poll::Pending
        }
    }
}

pub(crate) struct TimerHandle {
    inner: Weak<(AtomicWaker, AtomicBool)>,
}

impl TimerHandle {
    pub fn is_defunct(&self) -> bool {
        self.inner.upgrade().is_none()
    }

    pub fn wake(&self) {
        if let Some(wb) = self.inner.upgrade() {
            wb.1.store(true, Ordering::SeqCst);
            wb.0.wake();
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

#[derive(Default)]
pub(crate) struct TimerHeap {
    inner: BinaryHeap<TimeWaker>,
}

impl TimerHeap {
    pub fn add_timer(&mut self, time: Time, handle: TimerHandle) {
        self.inner.push(TimeWaker { time, handle })
    }

    pub fn next_deadline(&mut self) -> Option<&TimeWaker> {
        while self.inner.peek().map(|t| t.handle.is_defunct()).unwrap_or_default() {
            self.inner.pop();
        }
        self.inner.peek()
    }

    pub fn pop(&mut self) -> Option<TimeWaker> {
        self.inner.pop()
    }

    /// Wake any expired timers, returning `true` if any are woken.
    pub fn wake_expired_timers(&mut self, now: Time) -> bool {
        let mut woke_something = false;
        while let Some(waker) = self.next_deadline().filter(|waker| waker.time() <= now) {
            waker.wake();
            self.pop();
            woke_something = true;
        }
        woke_something
    }
}

pub(crate) struct TimeWaker {
    time: Time,
    handle: TimerHandle,
}

impl TimeWaker {
    pub fn wake(&self) {
        self.handle.wake();
    }

    pub fn time(&self) -> Time {
        self.time
    }
}

impl Ord for TimeWaker {
    fn cmp(&self, other: &Self) -> cmp::Ordering {
        self.time.cmp(&other.time).reverse() // Reverse to get min-heap rather than max
    }
}

impl PartialOrd for TimeWaker {
    fn partial_cmp(&self, other: &Self) -> Option<cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl PartialEq for TimeWaker {
    /// BinaryHeap requires `TimeWaker: Ord` above so that there's a total ordering between
    /// elements, and `T: Ord` requires `T: Eq` even we don't actually need to check these for
    /// equality. We could use `Weak::ptr_eq` to check the handles here, but then that would cause
    /// the `PartialEq` implementation to return false in some cases where `Ord` returns
    /// `Ordering::Equal`, which is asking for logic errors down the line.
    fn eq(&self, other: &Self) -> bool {
        self.time == other.time
    }
}
impl Eq for TimeWaker {}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{
        temp::{Either, TempFutureExt},
        LocalExecutor, SendExecutor, TestExecutor, Timer,
    };
    use fuchsia_zircon::prelude::*;
    use fuchsia_zircon::Duration;
    use futures::prelude::*;

    #[test]
    fn shorter_fires_first() {
        let mut exec = LocalExecutor::new().unwrap();
        let shorter = Timer::new(Time::after(100.millis()));
        let longer = Timer::new(Time::after(1.second()));
        match exec.run_singlethreaded(shorter.select(longer)) {
            Either::Left(()) => {}
            Either::Right(()) => panic!("wrong timer fired"),
        }
    }

    #[test]
    fn starved_local_timers() {
        use futures_lite::future::yield_now;
        use std::{cell::Cell, rc::Rc, time::Duration};

        let mut exec = LocalExecutor::new().unwrap();
        let should_return: Rc<Cell<bool>> = Rc::new(Cell::new(false));
        let timer_duration = Duration::from_millis(500);
        let _blocked_on_timer = crate::Task::local({
            let should_return = Rc::clone(&should_return);
            async move {
                Timer::new(timer_duration).await;
                should_return.set(true);
            }
        });
        exec.run_singlethreaded(async move {
            while !should_return.get() {
                // wakes the task before yielding, exec can now schedule this task or others
                yield_now().await;

                // sleep to avoid wasting CPU, spinlooping would be equally valid for this test
                std::thread::sleep(timer_duration / 50);

                // this loop will time out if timers are starved
            }
        });
    }

    #[test]
    fn starved_send_timers() {
        use futures_lite::future::yield_now;
        use std::{
            sync::{
                atomic::{AtomicBool, Ordering},
                Arc,
            },
            time::Duration,
        };

        // create a send executor with only 1 thread so we can guarantee co-scheduling
        let mut exec = SendExecutor::new(1).unwrap();
        let should_return: Arc<AtomicBool> = Default::default();

        let timer_duration = Duration::from_millis(500);

        let _blocked_on_timer = crate::Task::spawn({
            let should_return = Arc::clone(&should_return);
            async move {
                Timer::new(timer_duration).await;
                should_return.store(true, Ordering::SeqCst);
            }
        });
        exec.run(async move {
            while !should_return.load(Ordering::SeqCst) {
                // wakes the task before yielding, exec can now schedule this task or others
                yield_now().await;

                // sleep to avoid wasting CPU, spinlooping would be equally valid for this test
                std::thread::sleep(timer_duration / 50);

                // this loop will time out if timers are starved
            }
        });
    }

    #[test]
    fn shorter_fires_first_multithreaded() {
        let mut exec = SendExecutor::new(4).unwrap();
        let shorter = Timer::new(Time::after(100.millis()));
        let longer = Timer::new(Time::after(1.second()));
        match exec.run(shorter.select(longer)) {
            Either::Left(()) => {}
            Either::Right(()) => panic!("wrong timer fired"),
        }
    }

    #[test]
    fn fires_after_timeout() {
        let mut exec = TestExecutor::new().unwrap();
        let deadline = Time::after(5.seconds());
        let mut future = Timer::new(deadline);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut future));
        assert_eq!(Some(deadline), exec.wake_next_timer());
        assert_eq!(Poll::Ready(()), exec.run_until_stalled(&mut future));
    }

    #[test]
    fn timer_before_now_fires_immediately() {
        let mut exec = TestExecutor::new().unwrap();
        let deadline = Time::from(Time::now() - Duration::from_nanos(1));
        let mut future = Timer::new(deadline);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut future));
        assert!(exec.wake_expired_timers());
        assert_eq!(Poll::Ready(()), exec.run_until_stalled(&mut future));
    }

    #[test]
    fn interval() {
        let mut exec = TestExecutor::new().unwrap();
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
        let mut exec = TestExecutor::new_with_fake_time().unwrap();
        exec.set_fake_time(Time::from_nanos(0));

        let mut timer = Timer::new(Time::after(1.seconds()));
        assert_eq!(exec.wake_expired_timers(), false);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut timer));

        exec.set_fake_time(Time::after(1.seconds()));
        assert_eq!(exec.wake_expired_timers(), true);
        assert_eq!(Poll::Ready(()), exec.run_until_stalled(&mut timer));
    }
}
