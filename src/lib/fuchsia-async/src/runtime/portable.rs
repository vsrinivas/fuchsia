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
    pub struct Task<T>(async_executor::Task<T>);

    impl<T: 'static> Task<T> {
        /// Spawn a new `Send` task onto the current executor.
        ///
        /// # Panics
        ///
        /// `spawn` may panic if not called in the context of an executor (e.g.
        /// within a call to `run` or `run_singlethreaded`).
        pub fn spawn(fut: impl Future<Output = T> + Send + 'static) -> Self
        where
            T: Send,
        {
            Self(super::executor::spawn(fut))
        }

        /// Spawn a new non-`Send` task onto the single threaded executor.
        ///
        /// # Panics
        ///
        /// `local` may panic if not called in the context of a local executor
        /// (e.g. within a call to `run` or `run_singlethreaded`).
        pub fn local<'a>(fut: impl Future<Output = T> + 'static) -> Self {
            Self(super::executor::local(fut))
        }

        /// detach the Task handle. The contained future will be polled until completion.
        pub fn detach(self) {
            self.0.detach()
        }

        /// cancel a task and wait for cancellation to complete.
        pub async fn cancel(self) -> Option<T> {
            self.0.cancel().await
        }
    }

    impl<T> Future for Task<T> {
        type Output = T;

        fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
            use futures_lite::FutureExt;
            self.0.poll(cx)
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
    pub fn unblock<T: 'static + Send>(
        f: impl 'static + Send + FnOnce() -> T,
    ) -> impl 'static + Send + Future<Output = T> {
        blocking::unblock(f)
    }
}

pub mod executor {
    use crate::runtime::WakeupTime;
    use easy_parallel::Parallel;
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
    ) -> async_executor::Task<T>
    where
        T: Send,
    {
        GLOBAL.spawn(fut)
    }

    pub(crate) fn local<T>(fut: impl Future<Output = T> + 'static) -> async_executor::Task<T>
    where
        T: 'static,
    {
        LOCAL.with(|local| local.spawn(fut))
    }

    thread_local! {
        static LOCAL: async_executor::LocalExecutor<'static> = async_executor::LocalExecutor::new();
    }

    static GLOBAL: async_executor::Executor<'_> = async_executor::Executor::new();

    /// A multi-threaded executor.
    ///
    /// API-compatible with the Fuchsia variant.
    ///
    /// The current implementation of Executor does not isolate work
    /// (as the underlying executor is not yet capable of this).
    pub struct SendExecutor {
        num_threads: usize,
    }

    impl SendExecutor {
        /// Create a new executor running with actual time.
        pub fn new(num_threads: usize) -> Result<Self, zx_status::Status> {
            Ok(Self { num_threads })
        }

        /// Run a single future to completion using multiple threads.
        pub fn run<F>(&mut self, main_future: F) -> F::Output
        where
            F: Future + Send + 'static,
            F::Output: Send + 'static,
        {
            let (signal, shutdown) = async_channel::unbounded::<()>();

            let (_, res) = Parallel::new()
                .each(0..self.num_threads, |_| {
                    LOCAL.with(|local| {
                        let _ = async_io::block_on(local.run(GLOBAL.run(shutdown.recv())));
                    })
                })
                .finish(|| {
                    LOCAL.with(|local| {
                        async_io::block_on(local.run(GLOBAL.run(async {
                            let res = main_future.await;
                            drop(signal);
                            res
                        })))
                    })
                });
            res
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
            LOCAL.with(|local| async_io::block_on(GLOBAL.run(local.run(main_future))))
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
