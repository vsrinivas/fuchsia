// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::{ready, select, BoxFuture, Either, FusedFuture};
use futures::prelude::*;
use futures::task::{Context, Poll, Waker};
use slab::Slab;
use std::pin::Pin;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;

/// An asynchronous condition that can block multiple tasks
/// until triggered.
#[derive(Debug)]
pub struct AsyncCondition {
    wakers: Mutex<Slab<Waker>>,
    trigger_counter: AtomicUsize,
}

impl Default for AsyncCondition {
    fn default() -> Self {
        AsyncCondition::new()
    }
}

impl AsyncCondition {
    /// Creates a new `AsyncCondition` instance.
    pub fn new() -> AsyncCondition {
        AsyncCondition {
            wakers: Mutex::new(Slab::with_capacity(4)),
            trigger_counter: AtomicUsize::new(1),
        }
    }

    /// Returns a future that will block until `trigger()` is next called.
    pub fn wait(&self) -> AsyncConditionWait<'_> {
        AsyncConditionWait {
            condition: self,
            key: None,
            trigger_after: self.trigger_counter.load(Ordering::Acquire),
        }
    }

    /// Awakes all pending `AsyncConditionWait` instances vended by the `wait()`
    /// command.
    pub fn trigger(&self) {
        let inner = self.wakers.lock().unwrap();
        self.trigger_counter.fetch_add(1, Ordering::AcqRel);
        for (_, waker) in inner.iter() {
            waker.wake_by_ref();
        }
    }
}

/// Instance of `Future` returned by `AsyncCondition.wait()`.
#[must_use = "futures do nothing unless polled"]
#[derive(Debug)]
pub struct AsyncConditionWait<'a> {
    condition: &'a AsyncCondition,
    key: Option<usize>,
    trigger_after: usize,
}

impl<'a> Future for AsyncConditionWait<'a> {
    type Output = ();
    fn poll(mut self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<Self::Output> {
        if self.is_terminated() {
            return Poll::Ready(());
        }

        if self.trigger_after != self.condition.trigger_counter.load(Ordering::Acquire) {
            self.trigger_after = 0;
            return Poll::Ready(());
        }

        let mut wakers = self.condition.wakers.lock().unwrap();

        if let Some(slot) = self.key.and_then(|k| wakers.get_mut(k)) {
            // Update the waker already in the slab.
            *slot = context.waker().clone();
        } else {
            // Add the waker to the slab.
            self.key = Some(wakers.insert(context.waker().clone()));
        }

        Poll::Pending
    }
}

impl<'a> FusedFuture for AsyncConditionWait<'a> {
    fn is_terminated(&self) -> bool {
        self.trigger_after == 0
    }
}

impl<'a> Drop for AsyncConditionWait<'a> {
    fn drop(&mut self) {
        // Remove any previous waker from the slab.
        if let Some(key) = self.key {
            let mut wakers = self.condition.wakers.lock().unwrap();
            assert!(wakers.contains(key), "AsyncConditionWait contained invalid waker key");
            wakers.remove(key);
        }
    }
}

/// An extension trait to the `Future` trait that enables short circuiting.
///
/// All types that implement `Future` will receive a blanket implementation of this trait..
pub trait FutureExt: Future {
    /// Short circuits the given future by returning a specific value if/when the provided
    /// condition is "ready".
    fn cancel_upon<'a, F>(
        self,
        condition: F,
        cancel_value: Self::Output,
    ) -> BoxFuture<'a, Self::Output>
    where
        Self: Sized + Unpin + Send + 'a,
        Self::Output: Send + 'a,
        F: Future<Output = ()> + Unpin + Send + 'a,
    {
        select(self, condition)
            .then(move |x| match x {
                Either::Left((x, _)) => ready(x),
                Either::Right(_) => ready(cancel_value),
            })
            .boxed()
    }
}

// Implements the `FutureExt` trait for all types that implement `Future`.
impl<T: ?Sized> FutureExt for T where T: Future {}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    #[fasync::run_until_stalled(test)]
    async fn test_async_condition() {
        let condition = AsyncCondition::new();

        assert_eq!(condition.wait().now_or_never(), None);

        let waiter = condition.wait();

        condition.trigger();

        assert_eq!(waiter.now_or_never(), Some(()));

        let waiter = condition.wait();

        futures::join!(waiter, async {
            condition.trigger();
        });
    }

    /// This test makes sure that all of the `Waker`
    /// logic is working properly by spawning a task
    /// on a separate thread that waits for a
    /// condition to be triggered and then triggers
    /// a separate condition.
    #[cfg(feature = "thread-pool")]
    #[test]
    fn test_async_condition_thread_pool() {
        use futures::executor::ThreadPool;

        let condition1 = AsyncCondition::new();
        let condition2 = AsyncCondition::new();
        let pool = ThreadPool::new().expect("Unable to create thread pool");

        let wait1 = condition1.wait();
        let wait2a = condition2.wait();
        let wait2b = condition2.wait();

        pool.spawn_ok(async move {
            wait1.await;
            condition2.trigger();
        });

        assert_eq!(wait2a.now_or_never(), None);

        condition1.trigger();

        assert_eq!(wait2b.now_or_never(), Some(()));
    }

    #[test]
    fn test_cancel_upon() {
        let condition = AsyncCondition::new();

        let pending = futures::future::pending::<()>().cancel_upon(condition.wait(), ());

        assert_eq!(pending.now_or_never(), None);

        let pending = futures::future::pending::<()>().cancel_upon(condition.wait(), ());

        condition.trigger();

        assert_eq!(pending.now_or_never(), Some(()));
    }
}
