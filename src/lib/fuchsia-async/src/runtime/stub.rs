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
}

pub mod executor {
    /// Stub spawn, panics if used.
    //pub fn spawn<T>(_: T) {
    //    unimplemented!()
    //}

    /// Stub spawn local, panics if used.
    //pub fn spawn_local<T>(_: T) {
    //    unimplemented!()
    //}

    /// Stub Executor.
    pub struct Executor {}

    impl Executor {
        /// Stub run, panics if used.
        pub fn run<F>(&mut self, _: F, __: usize) -> F::Output
        where
            F: core::future::Future + Send + 'static,
            F::Output: Send + 'static,
        {
            unimplemented!();
        }

        /// Stub run_singlethreaded, panics if used.
        pub fn run_singlethreaded<F>(&mut self, _: F) -> F::Output
        where
            F: core::future::Future + 'static,
        {
            unimplemented!();
        }
    }

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
