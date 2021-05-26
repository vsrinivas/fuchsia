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
        // Fuse is a combinator that will drop the underlying future as soon as it has been
        // completed to ensure resources are reclaimed as soon as possible. That gives callers that
        // await on the Task the guarantee that the future has been dropped.
        //
        // Note that it is safe to pass in a future that has already been fused. Double fusing
        // a future does not change the expected behavior.
        let future = future.fuse();
        let (future, remote_handle) = future.remote_handle();
        super::executor::spawn(future);
        Task { remote_handle }
    }

    /// Spawn a new task backed by a thread
    /// TODO: Consider using a backing thread pool to alleviate the cost of spawning new threads
    /// if this proves to be a bottleneck.
    pub fn blocking(future: impl Future<Output = T> + Send + 'static) -> Task<T> {
        // Fuse is a combinator that will drop the underlying future as soon as it has been
        // completed to ensure resources are reclaimed as soon as possible. That gives callers that
        // await on the Task the guarantee that the future has been dropped.
        //
        // This is especially important for the `blocking` call which starts up an executor in
        // another thread.
        //
        // For example, if a receiver was registered on the main executor and its
        // ReceiverRegistration object is moved into a Task that runs on a different executor,
        // then the result of that Task should only be sent *after* the ReceiverRegistration object
        // has been destroyed. Otherwise there can be a race between the main executor shutting
        // down on one thread and the receiver being deregistered on another.
        //
        // Note that it is safe to pass in a future that has already been fused. Double fusing
        // a future does not change the expected behavior.
        let future = future.fuse();
        let (future, remote_handle) = future.remote_handle();
        std::thread::spawn(move || {
            super::executor::LocalExecutor::new().unwrap().run_singlethreaded(future)
        });
        Task { remote_handle }
    }
}

impl<T> Task<T> {
    /// Spawn a new task on the thread local executor.
    pub fn local(future: impl Future<Output = T> + 'static) -> Task<T> {
        // Fuse is a combinator that will drop the underlying future as soon as it has been
        // completed to ensure resources are reclaimed as soon as possible. That gives callers that
        // await on the Task the guarantee that the future has been dropped.
        //
        // Note that it is safe to pass in a future that has already been fused. Double fusing
        // a future does not change the expected behavior.
        let future = future.fuse();
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

#[cfg(test)]
mod tests {
    use super::super::executor::LocalExecutor;
    use super::*;
    use std::sync::{Arc, Mutex};

    /// This struct holds a thread-safe mutable boolean and
    /// sets its value to true when dropped.
    #[derive(Clone)]
    struct SetsBoolTrueOnDrop {
        value: Arc<Mutex<bool>>,
    }

    impl SetsBoolTrueOnDrop {
        fn new() -> (Self, Arc<Mutex<bool>>) {
            let value = Arc::new(Mutex::new(false));
            let sets_bool_true_on_drop = Self { value: value.clone() };
            (sets_bool_true_on_drop, value)
        }
    }

    impl Drop for SetsBoolTrueOnDrop {
        fn drop(&mut self) {
            let mut lock = self.value.lock().unwrap();
            *lock = true;
        }
    }

    #[test]
    fn future_destroyed_before_await_returns() {
        LocalExecutor::new().unwrap().run_singlethreaded(async {
            let (sets_bool_true_on_drop, value) = SetsBoolTrueOnDrop::new();

            // Move the switch into a future that runs on a different thread.
            // Once we return from this await, that switch should have been dropped.
            Task::blocking(async move {
                let lock = sets_bool_true_on_drop.value.lock().unwrap();
                assert_eq!(*lock, false);
            })
            .await;

            // Switch moved into the future should have been dropped at this point.
            // The value of the boolean should now be true.
            let lock = value.lock().unwrap();
            assert_eq!(*lock, true);
        });
    }

    #[test]
    fn fused_future_passed_into_task() {
        LocalExecutor::new().unwrap().run_singlethreaded(async {
            let (sets_bool_true_on_drop, value) = SetsBoolTrueOnDrop::new();

            // The fused future passed in here gets double fused. This should not
            // change the expected behavior.
            Task::blocking(
                async move {
                    let lock = sets_bool_true_on_drop.value.lock().unwrap();
                    assert_eq!(*lock, false);
                }
                .fuse(),
            )
            .await;

            // Switch moved into the future should have been dropped at this point.
            // The value of the boolean should now be true.
            let lock = value.lock().unwrap();
            assert_eq!(*lock, true);
        });
    }
}
