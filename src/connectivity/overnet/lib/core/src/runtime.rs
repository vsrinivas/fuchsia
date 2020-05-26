// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Instant;

pub async fn maybe_wait_until(t: Option<Instant>) {
    if let Some(t) = t {
        wait_until(t).await
    } else {
        futures::future::pending().await
    }
}

#[cfg(not(target_os = "fuchsia"))]
mod host_runtime {
    use futures::prelude::*;
    use std::time::Instant;

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

    /// Wait until some fixed time in the future
    pub async fn wait_until(t: Instant) {
        let _ = smol::Timer::at(t).await;
    }

    /// Run until `future` finishes
    pub fn run<R: Send + 'static>(future: impl Send + Future<Output = R> + 'static) -> R {
        smol::run(future)
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
    use std::time::Instant;

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

    /// Wait until some fixed time in the future
    pub async fn wait_until(t: Instant) {
        let now = Instant::now();
        if t <= now {
            return;
        }
        fuchsia_async::Timer::new(fuchsia_zircon::Time::after((t - now).into()).into()).await;
    }

    /// Run until `future` finishes
    pub fn run<R: Send + 'static>(future: impl Send + Future<Output = R> + 'static) -> R {
        fuchsia_async::Executor::new().unwrap().run(future, 4)
    }
}

#[cfg(target_os = "fuchsia")]
pub use fuchsia_runtime::*;
