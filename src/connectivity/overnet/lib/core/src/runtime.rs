// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::prelude::*;
use std::{
    pin::Pin,
    task::{Context, Poll},
    time::{Duration, Instant},
};

/// Operation timed out
#[derive(thiserror::Error, Debug)]
#[error("Timeout")]
pub struct TimeoutError;

pub struct MaybeTimer(Option<Timer>);

impl Future for MaybeTimer {
    type Output = ();
    fn poll(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
        match &mut self.get_mut().0 {
            None => Poll::Pending,
            Some(t) => t.poll_unpin(ctx),
        }
    }
}

#[must_use]
pub fn maybe_wait_until(t: Option<Instant>) -> MaybeTimer {
    MaybeTimer(t.map(|t| Timer::at(t)))
}

/// Wait until some fixed time in the future
#[must_use]
pub fn wait_until(t: Instant) -> Timer {
    Timer::at(t)
}

/// Wait for some duration to pass
#[must_use]
pub fn wait_for(t: Duration) -> Timer {
    Timer::after(t)
}

/// Pending timeout future.
pub struct Timeout<Fut> {
    timer: Timer,
    fut: Fut,
}

impl<Fut: Future> Timeout<Fut> {
    fn pin_get_fut(self: Pin<&mut Self>) -> Pin<&mut Fut> {
        // This is okay because `fut` is pinned when `self` is.
        unsafe { self.map_unchecked_mut(|s| &mut s.fut) }
    }

    fn pin_get_timer(self: Pin<&mut Self>) -> Pin<&mut Timer> {
        // This is okay because `timer` is pinned when `self` is.
        unsafe { self.map_unchecked_mut(|s| &mut s.timer) }
    }
}

impl<Fut: Future> Future for Timeout<Fut> {
    type Output = Result<Fut::Output, TimeoutError>;
    fn poll(mut self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
        match self.as_mut().pin_get_fut().poll(ctx) {
            Poll::Ready(r) => Poll::Ready(Ok(r)),
            Poll::Pending => match self.as_mut().pin_get_timer().poll(ctx) {
                Poll::Ready(()) => Poll::Ready(Err(TimeoutError)),
                Poll::Pending => Poll::Pending,
            },
        }
    }
}

/// Extension methods for Future
pub trait FutureExt {
    /// A timeout method that works on any platform that Overnet works on.
    fn timeout_after(self, after: Duration) -> Timeout<Self>
    where
        Self: Sized;
}

impl<F: Future> FutureExt for F {
    fn timeout_after(self, after: Duration) -> Timeout<Self> {
        Timeout { timer: Timer::after(after), fut: self }
    }
}

#[cfg(not(target_os = "fuchsia"))]
mod host_runtime {
    use futures::{channel::oneshot, prelude::*};
    use std::{
        pin::Pin,
        task::{Context, Poll},
        thread::JoinHandle,
        time::{Duration, Instant},
    };

    /// A unit of concurrent execution... the contained future is stopped when the task is dropped
    pub struct Task(smol::Task<()>);

    impl Task {
        /// Spawn a task
        pub fn spawn(future: impl Future<Output = ()> + Send + 'static) -> Task {
            Task(smol::Task::spawn(future))
        }
        /// Detach the task so it can run in the background
        pub fn detach(self) {
            self.0.detach();
        }
    }

    /// A Timer is a future that finishes at some defined point in time.
    pub struct Timer(smol::Timer);

    impl Timer {
        /// Create a new timer that expires at `t`
        pub fn at(t: Instant) -> Timer {
            Timer(smol::Timer::at(t))
        }

        /// Create a new timer that expires after `t`
        pub fn after(t: Duration) -> Timer {
            Timer(smol::Timer::after(t))
        }
    }

    impl Future for Timer {
        type Output = ();
        fn poll(mut self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
            self.0.poll_unpin(ctx).map(drop)
        }
    }

    struct BackgroundThread {
        join_handle: Option<JoinHandle<()>>,
        terminate: Option<oneshot::Sender<()>>,
    }

    impl Drop for BackgroundThread {
        fn drop(&mut self) {
            self.terminate.take().unwrap().send(()).unwrap();
            self.join_handle.take().unwrap().join().unwrap();
        }
    }

    impl Default for BackgroundThread {
        fn default() -> Self {
            let (terminate, end_times) = oneshot::channel();
            let join_handle = std::thread::spawn(move || {
                smol::run(async move {
                    end_times.await.unwrap();
                });
            });
            Self { terminate: Some(terminate), join_handle: Some(join_handle) }
        }
    }

    lazy_static::lazy_static! {
        static ref BACKGROUND_THREADS: Vec<BackgroundThread> =
            std::iter::repeat_with(Default::default).take(
                std::env::var("OVERNET_THREADS").unwrap_or_default().parse().unwrap_or(16)
            ).collect();
    }

    /// Run until `future` finishes
    pub fn run<R: Send + 'static>(future: impl Send + Future<Output = R> + 'static) -> R {
        lazy_static::initialize(&BACKGROUND_THREADS);
        smol::run(future)
    }

    /// Run some code that may block
    pub async fn blocking<R: Send + 'static>(f: impl 'static + Send + Future<Output = R>) -> R {
        smol::blocking!(f).await
    }
}

#[cfg(not(target_os = "fuchsia"))]
pub use host_runtime::*;

#[cfg(target_os = "fuchsia")]
mod fuchsia_runtime {
    use futures::{
        future::{abortable, AbortHandle},
        prelude::*,
    };
    use std::{
        pin::Pin,
        task::{Context, Poll},
        time::{Duration, Instant},
    };

    /// A unit of concurrent execution... the contained future is stopped when the task is dropped
    pub struct Task {
        abort_handle: Option<AbortHandle>,
    }

    impl Drop for Task {
        fn drop(&mut self) {
            self.abort_handle.take().map(|h| h.abort());
        }
    }

    impl Task {
        /// Spawn a task
        pub fn spawn(future: impl Future<Output = ()> + Send + 'static) -> Task {
            let (future, abort_handle) = abortable(future);
            fuchsia_async::spawn(future.map(drop));
            Task { abort_handle: Some(abort_handle) }
        }

        /// Detach the task so it can run in the background
        pub fn detach(mut self) {
            self.abort_handle = None;
        }
    }

    /// A Timer is a future that finishes at some defined point in time.
    pub struct Timer(fuchsia_async::Timer);

    impl Timer {
        /// Create a new timer that expires at `t`
        pub fn at(t: Instant) -> Timer {
            Timer(fuchsia_async::Timer::new(t))
        }

        /// Create a new timer that expires after `t`
        pub fn after(t: Duration) -> Timer {
            Timer(fuchsia_async::Timer::new(fuchsia_async::Time::after(t.into())))
        }
    }

    impl Future for Timer {
        type Output = ();
        fn poll(mut self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
            self.0.poll_unpin(ctx)
        }
    }

    /// Run until `future` finishes
    pub fn run<R: Send + 'static>(future: impl Send + Future<Output = R> + 'static) -> R {
        fuchsia_async::Executor::new().unwrap().run_singlethreaded(future)
    }

    /// Run some code that may block
    pub async fn blocking<R: Send + 'static>(f: impl 'static + Send + Future<Output = R>) -> R {
        f.await
    }
}

#[cfg(target_os = "fuchsia")]
pub use fuchsia_runtime::*;
