// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod task {
    use core::task::{Context, Poll};
    use std::future::Future;
    use std::pin::Pin;

    /// A handle to a task.
    ///
    /// A task can be polled for the output of the future it is executing. A
    /// dropped task will be cancelled after dropping. To immediately cancel a
    /// task, call the cancel() method. To run a task to completion without
    /// retaining the Task handle, call the detach() method.
    #[derive(Debug)]
    pub struct Task<T>(pub(crate) Option<tokio::task::JoinHandle<T>>);

    impl<T: 'static> Task<T> {
        /// spawn a new `Send` task onto the executor.
        pub fn spawn(fut: impl Future<Output = T> + Send + 'static) -> Self
        where
            T: Send,
        {
            Self(Some(super::executor::spawn(fut)))
        }

        /// spawn a new non-`Send` task onto the single threaded executor.
        pub fn local<'a>(fut: impl Future<Output = T> + 'static) -> Self {
            Self(Some(super::executor::local(fut)))
        }

        /// detach the Task handle. The contained future will be polled until completion.
        pub fn detach(mut self) {
            self.0.take();
        }

        /// cancel a task and wait for cancellation to complete.
        pub async fn cancel(self) -> Option<T> {
            match self.0 {
                None => None,
                Some(join_handle) => {
                    join_handle.abort();
                    join_handle.await.ok()
                }
            }
        }
    }

    impl<T> Future for Task<T> {
        type Output = T;

        fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
            // TODO: spawning a task onto a task may leak, never resolving
            use futures_lite::FutureExt;
            self.0.as_mut().map_or(Poll::Pending, |jh| match jh.poll(cx) {
                Poll::Ready(Ok(r)) => Poll::Ready(r),
                _ => Poll::Pending,
            })
        }
    }

    /// Offload a blocking function call onto a different thread.
    ///
    /// This function can be called from an asynchronous function without blocking
    /// it, returning a future that can be `.await`ed normally. The provided
    /// function should contain at least one blocking operation, such as:
    ///
    /// - A synchronous syscall that does not yet have an async counterpart.
    /// - A compute operation which risks blocking the executor for an unacceptable
    ///   amount of time.
    ///
    /// If neither of these conditions are satisfied, just call the function normally,
    /// as synchronous functions themselves are allowed within an async context,
    /// as long as they are not blocking.
    ///
    /// If you have an async function that may block, refactor the function such that
    /// the blocking operations are offloaded onto the function passed to [`unblock`].
    ///
    /// NOTE: Synchronous functions cannot be cancelled and may keep running after
    /// the returned future is dropped. As a result, resources held by the function
    /// should be assumed to be held until the returned future completes.
    ///
    /// For details on performance characteristics and edge cases, see [`blocking::unblock`].
    // TODO: redo docs
    pub fn unblock<T: 'static + Send>(
        f: impl 'static + Send + FnOnce() -> T,
    ) -> impl 'static + Send + Future<Output = T> {
        crate::Task(Some(tokio::task::spawn_blocking(f)))
    }
}

pub mod executor {
    use crate::runtime::WakeupTime;
    use fuchsia_zircon_status as zx_status;
    use std::future::Future;

    pub use std::time::Duration;
    /// A time relative to the executor's clock.
    pub use std::time::Instant as Time;

    impl WakeupTime for Time {
        fn into_time(self) -> Time {
            self
        }
    }

    pub(crate) fn spawn<T: 'static>(
        fut: impl Future<Output = T> + Send + 'static,
    ) -> tokio::task::JoinHandle<T>
    where
        T: Send,
    {
        tokio::task::spawn(fut)
    }

    pub(crate) fn local<T>(fut: impl Future<Output = T> + 'static) -> tokio::task::JoinHandle<T>
    where
        T: 'static,
    {
        LOCAL.with(|local| local.spawn_local(fut))
    }

    thread_local! {
        static LOCAL: tokio::task::LocalSet = tokio::task::LocalSet::new();
    }

    /// A multi-threaded executor.
    ///
    /// API-compatible with the Fuchsia variant.
    ///
    /// The current implementation of Executor does not isolate work
    /// (as the underlying executor is not yet capable of this).
    pub struct SendExecutor {
        runtime: tokio::runtime::Runtime,
    }

    impl SendExecutor {
        /// Create a new executor running with actual time.
        pub fn new(num_threads: usize) -> Result<Self, zx_status::Status> {
            Ok(Self {
                runtime: tokio::runtime::Builder::new_multi_thread()
                    .worker_threads(num_threads)
                    .enable_all()
                    .build()
                    // TODO: how to better report errors given the API constraints?
                    .map_err(|_e| zx_status::Status::IO)?,
            })
        }

        /// Run a single future to completion using multiple threads.
        pub fn run<F>(&mut self, main_future: F) -> F::Output
        where
            F: Future + Send + 'static,
            F::Output: Send + 'static,
        {
            LOCAL.with(|local| local.block_on(&self.runtime, main_future))
        }
    }

    /// A single-threaded executor.
    ///
    /// API-compatible with the Fuchsia variant with the exception of testing APIs.
    ///
    /// The current implementation of Executor does not isolate work
    /// (as the underlying executor is not yet capable of this).
    pub struct LocalExecutor {}

    impl LocalExecutor {
        /// Create a new executor.
        pub fn new() -> Result<Self, zx_status::Status> {
            Ok(Self {})
        }

        /// Run a single future to completion on a single thread.
        pub fn run_singlethreaded<F>(&mut self, main_future: F) -> F::Output
        where
            F: Future,
        {
            LOCAL.with(|local| {
                local.block_on(
                    &tokio::runtime::Builder::new_current_thread().build().unwrap(),
                    main_future,
                )
            })
        }
    }

    /// A single-threaded executor for testing.
    ///
    /// The current implementation of Executor does not isolate work
    /// (as the underlying executor is not yet capable of this).
    pub struct TestExecutor {}

    impl TestExecutor {
        /// Create a new executor for testing.
        pub fn new() -> Result<Self, zx_status::Status> {
            Ok(Self {})
        }

        /// Run a single future to completion on a single thread.
        pub fn run_singlethreaded<F>(&mut self, main_future: F) -> F::Output
        where
            F: Future,
        {
            LocalExecutor {}.run_singlethreaded(main_future)
        }
    }
}

pub mod timer {
    use crate::runtime::WakeupTime;
    use futures::prelude::*;
    use std::pin::Pin;
    use std::task::{Context, Poll};

    /// An asynchronous timer.
    #[derive(Debug)]
    #[must_use = "futures do nothing unless polled"]
    pub struct Timer(async_io::Timer);

    impl Timer {
        /// Create a new timer scheduled to fire at `time`.
        pub fn new<WT>(time: WT) -> Self
        where
            WT: WakeupTime,
        {
            Timer(async_io::Timer::at(time.into_time()))
        }
    }

    impl Future for Timer {
        type Output = ();
        fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
            self.0.poll_unpin(cx).map(drop)
        }
    }
}
