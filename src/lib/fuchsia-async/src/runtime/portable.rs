// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod task {
    pub use smol::Task;
}

pub mod executor {
    use crate::runtime::WakeupTime;
    use fuchsia_zircon_status as zx_status;
    use futures::channel::oneshot;
    use futures::prelude::*;
    use std::thread::JoinHandle;

    /// A time relative to the executor's clock.
    pub use std::time::Instant as Time;

    impl WakeupTime for Time {
        fn into_time(self) -> Time {
            self
        }
    }

    struct ExecutorThread {
        join_handle: Option<JoinHandle<()>>,
        terminate: Option<oneshot::Sender<()>>,
    }

    impl Drop for ExecutorThread {
        fn drop(&mut self) {
            self.terminate.take().unwrap().send(()).unwrap();
            self.join_handle.take().unwrap().join().unwrap();
        }
    }

    impl Default for ExecutorThread {
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

    /// An executor.
    /// Mostly API-compatible with the Fuchsia variant (without the run_until_stalled or
    /// fake time pieces).
    /// The current implementation of Executor does not isolate work
    /// (as the underlying smol executor is not yet capable of this).
    pub struct Executor;

    impl Executor {
        /// Create a new executor running with actual time.
        pub fn new() -> Result<Self, zx_status::Status> {
            Ok(Self)
        }

        /// Run a single future to completion using multiple threads.
        // Takes `&mut self` to ensure that only one thread-manager is running at a time.
        pub fn run<F>(&mut self, future: F, num_threads: usize) -> F::Output
        where
            F: Future + Send + 'static,
            F::Output: Send + 'static,
        {
            let _threads: Vec<ExecutorThread> =
                std::iter::repeat_with(Default::default).take(num_threads).collect();
            smol::block_on(future)
        }

        /// Run a single future to completion on a single thread.
        // Takes `&mut self` to ensure that only one thread-manager is running at a time.
        pub fn run_singlethreaded<F>(&mut self, main_future: F) -> F::Output
        where
            F: Future + 'static,
        {
            smol::run(main_future)
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
    pub struct Timer(smol::Timer);

    impl Timer {
        /// Create a new timer scheduled to fire at `time`.
        pub fn new<WT>(time: WT) -> Self
        where
            WT: WakeupTime,
        {
            Timer(smol::Timer::at(time.into_time()))
        }
    }

    impl Future for Timer {
        type Output = ();
        fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
            self.0.poll_unpin(cx).map(drop)
        }
    }
}
