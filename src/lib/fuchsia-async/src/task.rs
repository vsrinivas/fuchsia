// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::RemoteHandle;
use futures::prelude::*;
use std::pin::Pin;
use std::task::{Context, Poll};

/// A spawned future.
/// When a task is dropped the future will stop being polled by the executor.
pub struct Task<T> {
    remote_handle: RemoteHandle<T>,
}

impl Task<()> {
    /// Detach this task so that it can run independently in the background.
    pub fn detach(self) {
        self.remote_handle.forget();
    }
}

impl<T: Send> Task<T> {
    /// Spawn a new task on the current executor.
    pub fn spawn(future: impl Future<Output = T> + Send + 'static) -> Task<T> {
        let (future, remote_handle) = future.remote_handle();
        crate::executor::spawn(future);
        Task { remote_handle }
    }
}

impl<T> Task<T> {
    /// Spawn a new task on the thread local executor.
    pub fn local(future: impl Future<Output = T> + 'static) -> Task<T> {
        let (future, remote_handle) = future.remote_handle();
        crate::executor::spawn_local(future);
        Task { remote_handle }
    }
}

impl<T: 'static> Task<T> {
    /// Cancel this task.
    /// Returns the tasks output if it was available prior to cancelation.
    pub fn cancel(self) -> Option<T> {
        self.remote_handle.now_or_never()
    }
}

impl<T: 'static> Future for Task<T> {
    type Output = T;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.remote_handle.poll_unpin(cx)
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use crate::executor::Executor;
    use futures::channel::oneshot;

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
            t.cancel();
            // we should see an error on receive
            rx_done.await.expect_err("done should not be sent");
        })
    }
}
