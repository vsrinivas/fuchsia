// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::RemoteHandle;
use futures::prelude::*;
use std::pin::Pin;
use std::task::{Context, Poll};

/// A spawned future.
/// When a task is dropped the future will stop being polled by the executor.
#[must_use]
pub struct Task<T> {
    remote_handle: RemoteHandle<T>,
}

impl Task<()> {
    /// Detach this task so that it can run independently in the background.
    ///
    /// *Note*: this is usually not what you want. This API severs the control flow from the
    /// caller, making it impossible to return values (including errors). If your goal is to run
    /// multiple futures concurrently, consider if futures combinators such as
    ///
    /// * [`futures::future::join`]
    /// * [`futures::future::select`]
    /// * [`futures::select`]
    ///
    /// their error-aware variants
    ///
    /// * [`futures::future::try_join`]
    /// * [`futures::future::try_select`]
    ///
    /// or their stream counterparts
    ///
    /// * [`futures::stream::StreamExt::for_each`]
    /// * [`futures::stream::StreamExt::for_each_concurrent`]
    /// * [`futures::stream::TryStreamExt::try_for_each`]
    /// * [`futures::stream::TryStreamExt::try_for_each_concurrent`]
    ///
    /// can meet your needs.
    pub fn detach(self) {
        self.remote_handle.forget();
    }
}

impl<T: Send> Task<T> {
    /// Spawn a new task on the current executor. The passed future will live until
    /// it completes, the returned `Task` is dropped while the executor is running,
    /// or the executor is destroyed, whichever comes first.
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
