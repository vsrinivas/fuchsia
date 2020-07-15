// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a stub runtime that panics if it is ever used.
// TODO(56078): Remove this once smol supports WASM.

pub mod task {
    /// Stub Task.
    pub struct Task {}
}

pub mod executor {
    /// Stub spawn, panics if used.
    pub fn spawn<T>(_: T) {
        unimplemented!()
    }

    /// Stub spawn local, panics if used.
    pub fn spawn_local<T>(_: T) {
        unimplemented!()
    }

    /// Stub Executor.
    pub struct Executor {}

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
