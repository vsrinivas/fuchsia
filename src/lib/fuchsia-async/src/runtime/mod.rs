// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(target_os = "fuchsia")]
mod fuchsia;
#[cfg(target_os = "fuchsia")]
use self::fuchsia as implementation;

#[cfg(all(not(target_os = "fuchsia"), not(target_arch = "wasm32")))]
mod portable;
#[cfg(all(not(target_os = "fuchsia"), not(target_arch = "wasm32")))]
use self::portable as implementation;

#[cfg(all(not(target_os = "fuchsia"), target_arch = "wasm32"))]
mod stub;
#[cfg(all(not(target_os = "fuchsia"), target_arch = "wasm32"))]
use self::stub as implementation;

pub use implementation::{
    executor::{Executor, Time},
    task::Task,
    timer::Timer,
};

// Fuchsia specific exports
#[cfg(target_os = "fuchsia")]
pub use self::fuchsia::{
    executor::{EHandle, PacketReceiver, ReceiverRegistration, WaitState},
    timer::Interval,
};

#[cfg(target_os = "fuchsia")]
pub(crate) use self::fuchsia::executor::{need_signal, schedule_packet};

use futures::prelude::*;
use pin_utils::{unsafe_pinned, unsafe_unpinned};
use std::pin::Pin;
use std::task::{Context, Poll};

/// An extension trait to provide `after_now` on `zx::Duration`.
pub trait DurationExt {
    /// Return a `Time` which is a `Duration` after the current time.
    /// `duration.after_now()` is equivalent to `Time::after(duration)`.
    ///
    /// This method requires that an executor has been set up.
    fn after_now(self) -> Time;
}

/// The time when a Timer should wakeup.
pub trait WakeupTime {
    /// Convert this time into a fuchsia_async::Time.
    /// This is allowed to be innacurate, but the innacuracy must make the wakeup time later,
    /// never earlier.
    fn into_time(self) -> Time;
}

impl WakeupTime for std::time::Duration {
    fn into_time(self) -> Time {
        Time::now() + self.into()
    }
}

/// A trait which allows futures to be easily wrapped in a timeout.
pub trait TimeoutExt: Future + Sized {
    /// Wraps the future in a timeout, calling `on_timeout` to produce a result
    /// when the timeout occurs.
    fn on_timeout<WT, OT>(self, time: WT, on_timeout: OT) -> OnTimeout<Self, OT>
    where
        WT: WakeupTime,
        OT: FnOnce() -> Self::Output,
    {
        OnTimeout { timer: Timer::new(time), future: self, on_timeout: Some(on_timeout) }
    }

    /// Wraps the future in a stall-guard, calling `on_stalled` to produce a result
    /// when the future hasn't been otherwise polled within the `timeout`.
    /// This is a heuristic - spurious wakeups will keep the detection from triggering,
    /// and moving all work to external tasks or threads with force the triggering early.
    fn on_stalled<OS>(self, timeout: std::time::Duration, on_stalled: OS) -> OnStalled<Self, OS>
    where
        OS: FnOnce() -> Self::Output,
    {
        OnStalled {
            timer: Timer::new(timeout),
            future: self,
            timeout,
            on_stalled: Some(on_stalled),
        }
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

/// A wrapper for a future who's steady progress is monitored and will complete with the provided
/// closure if no progress is made before the timeout.
#[derive(Debug)]
#[must_use = "futures do nothing unless polled"]
pub struct OnStalled<F, OS> {
    timer: Timer,
    future: F,
    timeout: std::time::Duration,
    on_stalled: Option<OS>,
}

impl<F, OS> OnStalled<F, OS> {
    // Safety: this is safe because `OnTimeout` is only `Unpin` if
    // the future is `Unpin`, and aside from `future`, all other fields are
    // treated as movable.
    unsafe_unpinned!(timer: Timer);
    unsafe_pinned!(future: F);
    unsafe_unpinned!(timeout: std::time::Duration);
    unsafe_unpinned!(on_stalled: Option<OS>);
}

impl<F: Unpin, OT> Unpin for OnStalled<F, OT> {}

impl<F: Future, OS> Future for OnStalled<F, OS>
where
    OS: FnOnce() -> F::Output,
{
    type Output = F::Output;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        if let Poll::Ready(item) = self.as_mut().future().poll(cx) {
            return Poll::Ready(item);
        }
        match self.as_mut().timer().poll_unpin(cx) {
            Poll::Ready(()) => {
                let os =
                    OnStalled::on_stalled(self.as_mut()).take().expect("polled after completion");
                Poll::Ready((os)())
            }
            Poll::Pending => {
                let new_timer = Timer::new(*self.as_mut().timeout());
                *self.as_mut().timer() = new_timer;
                Poll::Pending
            }
        }
    }
}

#[cfg(test)]
mod task_tests {

    use super::*;
    use crate::runtime::Executor;
    use futures::channel::oneshot;
    use std::future::Future;

    fn run(f: impl Send + 'static + Future<Output = ()>) {
        const TEST_THREADS: usize = 2;
        Executor::new().unwrap().run(f, TEST_THREADS)
    }

    #[test]
    fn can_detach() {
        run(async move {
            let (tx_started, rx_started) = oneshot::channel();
            let (tx_continue, rx_continue) = oneshot::channel();
            let (tx_done, rx_done) = oneshot::channel();
            {
                // spawn a task and detach it
                // the task will wait for a signal, signal it received it, and then wait for another
                Task::spawn(async move {
                    tx_started.send(()).unwrap();
                    rx_continue.await.unwrap();
                    tx_done.send(()).unwrap();
                })
                .detach();
            }
            // task is detached, have a short conversation with it
            rx_started.await.unwrap();
            tx_continue.send(()).unwrap();
            rx_done.await.unwrap();
        });
    }

    #[test]
    fn can_join() {
        // can we spawn, then join a task
        run(async move {
            assert_eq!(42, Task::spawn(async move { 42u8 }).await);
        })
    }

    #[test]
    fn can_join_blocking() {
        // can we spawn, then join a task
        run(async move {
            assert_eq!(42, Task::blocking(async move { 42u8 }).await);
        })
    }

    #[test]
    fn can_join_local() {
        // can we spawn, then join a task locally
        Executor::new().unwrap().run_singlethreaded(async move {
            assert_eq!(42, Task::local(async move { 42u8 }).await);
        })
    }

    #[test]
    fn can_cancel() {
        run(async move {
            let (_tx_start, rx_start) = oneshot::channel::<()>();
            let (tx_done, rx_done) = oneshot::channel();
            let t = Task::spawn(async move {
                rx_start.await.unwrap();
                tx_done.send(()).unwrap();
            });
            // cancel the task without sending the start signal
            t.cancel().await;
            // we should see an error on receive
            rx_done.await.expect_err("done should not be sent");
        })
    }
}

#[cfg(test)]
mod timer_tests {
    use super::*;
    use crate::temp::{Either, TempFutureExt};

    #[test]
    fn shorter_fires_first_instant() {
        use std::time::{Duration, Instant};
        let mut exec = Executor::new().unwrap();
        let now = Instant::now();
        let shorter = Timer::new(now + Duration::from_millis(100));
        let longer = Timer::new(now + Duration::from_secs(1));
        match exec.run_singlethreaded(shorter.select(longer)) {
            Either::Left(()) => {}
            Either::Right(()) => panic!("wrong timer fired"),
        }
    }

    #[test]
    fn can_detect_stalls() {
        use std::sync::atomic::{AtomicU64, Ordering};
        use std::sync::Arc;
        use std::time::Duration;
        let runs = Arc::new(AtomicU64::new(0));
        assert_eq!(
            {
                let runs = runs.clone();
                Executor::new().unwrap().run_singlethreaded(
                    async move {
                        let mut sleep = Duration::from_millis(1);
                        loop {
                            Timer::new(sleep).await;
                            sleep *= 2;
                            runs.fetch_add(1, Ordering::SeqCst);
                        }
                    }
                    .on_stalled(Duration::from_secs(1), || 1u8),
                )
            },
            1u8
        );
        assert!(runs.load(Ordering::SeqCst) >= 9);
    }
}
