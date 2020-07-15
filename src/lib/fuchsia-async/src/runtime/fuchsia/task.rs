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
        super::executor::spawn(future);
        Task { remote_handle }
    }

    /// Spawn a new task backed by a thread
    /// TODO: Consider using a backing thread pool to alleviate the cost of spawning new threads
    /// if this proves to be a bottleneck.
    pub fn blocking(future: impl Future<Output = T> + Send + 'static) -> Task<T> {
        let (future, remote_handle) = future.remote_handle();
        std::thread::spawn(move || {
            super::executor::Executor::new().unwrap().run_singlethreaded(future)
        });
        Task { remote_handle }
    }
}

impl<T> Task<T> {
    /// Spawn a new task on the thread local executor.
    pub fn local(future: impl Future<Output = T> + 'static) -> Task<T> {
        let (future, remote_handle) = future.remote_handle();
        super::executor::spawn_local(future);
        Task { remote_handle }
    }
}

impl<T: 'static> Task<T> {
    /// Cancel this task.
    /// Returns the tasks output if it was available prior to cancelation.
    pub async fn cancel(self) -> Option<T> {
        self.remote_handle.now_or_never()
    }
}

impl<T: 'static> Future for Task<T> {
    type Output = T;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.remote_handle.poll_unpin(cx)
    }
}
