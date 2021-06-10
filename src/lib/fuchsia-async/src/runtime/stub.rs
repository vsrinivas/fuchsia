// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a stub runtime that panics if it is ever used.
// TODO(fxbug.dev/56078): Remove this once smol supports WASM.

pub mod task {
    use futures::prelude::Future;
    /// Stub Task.
    pub struct Task<T> {
        _unused: T,
    }

    impl Task<()> {
        /// Stub detach, panics if used.
        pub fn detach(self) {
            unimplemented!();
        }
    }

    impl<T: Send> Task<T> {
        /// Stub blocking, panics if used.
        pub fn blocking(_: impl core::future::Future<Output = T> + Send + 'static) -> Task<T> {
            unimplemented!();
        }

        /// Stub spawn, panics if used.
        pub fn spawn(_: impl Future<Output = T> + Send + 'static) -> Task<T> {
            unimplemented!();
        }
    }

    impl<T> Task<T> {
        /// Stub local, panics if used.
        pub fn local(_: impl Future<Output = T> + 'static) -> Task<T> {
            unimplemented!();
        }
    }

    /// Stub unblock, panics if used.
    pub fn unblock<T: 'static + Send>(
        _: impl 'static + Send + FnOnce() -> T,
    ) -> impl 'static + Send + Future<Output = T> {
        // TODO(https://github.com/rust-lang/rust/issues/69882): Implement solution.
        async { unimplemented!() }
    }
}

pub mod executor {
    use fuchsia_zircon_status::Status;

    /// Stub spawn, panics if used.
    //pub fn spawn<T>(_: T) {
    //    unimplemented!()
    //}

    /// Stub spawn local, panics if used.
    //pub fn spawn_local<T>(_: T) {
    //    unimplemented!()
    //}

    /// A stub multi-threaded executor.
    pub struct SendExecutor {}

    impl SendExecutor {
        /// Stub new, panics if used.
        pub fn new(_: usize) -> Result<Self, Status> {
            unimplemented!();
        }

        /// Stub run, panics if used.
        pub fn run<F>(&mut self, _: F) -> F::Output
        where
            F: core::future::Future + Send + 'static,
            F::Output: Send + 'static,
        {
            unimplemented!();
        }
    }

    /// Wrapper around `Executor`, restricted to running single threaded.
    pub struct LocalExecutor {}

    impl LocalExecutor {
        /// Construct a new executor for running tasks on the current thread.
        pub fn new() -> Result<Self, Status> {
            unimplemented!();
        }

        /// Stub run_singlethreaded, panics if used.
        pub fn run_singlethreaded<F>(&mut self, _: F) -> F::Output
        where
            F: core::future::Future,
        {
            unimplemented!();
        }
    }

    /// Stub testing executor. Panics if used.
    pub struct TestExecutor {}

    impl TestExecutor {
        /// Construct a new stub executor, panics if used.
        pub fn new() -> Result<Self, Status> {
            unimplemented!();
        }

        /// Stub run_singlethreaded, panics if used.
        pub fn run_singlethreaded<F>(&mut self, _: F) -> F::Output
        where
            F: core::future::Future,
        {
            unimplemented!();
        }
    }

    pub use std::time::Duration;
    pub use std::time::Instant as Time;
}

pub mod timer {
    /// Stub Timer
    #[derive(Debug)]
    pub struct Timer {}

    impl Timer {
        /// Stub creation, panics if used.
        pub fn new<T>(_: T) -> Self {
            if true {
                unimplemented!();
            }
            Timer {}
        }
    }

    impl core::future::Future for Timer {
        type Output = ();

        /// Stub future implrementation, panics if used.
        fn poll(
            self: std::pin::Pin<&mut Self>,
            _: &mut std::task::Context<'_>,
        ) -> std::task::Poll<()> {
            if true {
                unimplemented!();
            }
            std::task::Poll::Ready(())
        }
    }
}
