// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::lock::{Mutex, MutexGuard, MutexLockFuture};
use futures::prelude::*;
use std::task::{Context, Poll};

/// Helper to poll a mutex.
///
/// Since Mutex::lock futures keep track of where in queue the lock request is,
/// this is different to `mutex.lock().poll(ctx)` as that construction will create
/// a new lock request at each poll.
/// This can often be useful when we need to poll something that is contained under
/// this mutex.
///
/// Typical usage:
///   let mut ticket = MutexTicket::new();
///   poll_fn(|ctx| {
///     let mutex_guard = ready!(ticket.poll(ctx));
///     mutex_guard.some_child_future.poll(ctx)
///   }).await;
///
/// What this means:
///   Attempt to acquire the mutex. If it's not available, wait until it's available.
///   With the mutex acquired, check some_child_future.
///   If it's completed, complete the poll_fn.
///   *If it's not completed* drop the mutex guard (unblock other tasks) and wait for
///   some_child_future to be awoken.
#[derive(Debug)]
pub struct MutexTicket<'a, T> {
    mutex: &'a Mutex<T>,
    lock: Option<MutexLockFuture<'a, T>>,
}

impl<'a, T> MutexTicket<'a, T> {
    /// Create a new `MutexTicket`
    pub fn new(mutex: &'a Mutex<T>) -> MutexTicket<'a, T> {
        MutexTicket { mutex, lock: None }
    }

    /// Poll once to see if the lock has been acquired.
    /// This is not Future::poll because it's intended to be a helper used during a Future::poll
    /// implementation, but never as a Future itself -- one can simply call Mutex::lock.await in that
    /// case!
    pub fn poll(&mut self, ctx: &mut Context<'_>) -> Poll<MutexGuard<'a, T>> {
        let mut lock_fut = match self.lock.take() {
            None => self.mutex.lock(),
            Some(lock_fut) => lock_fut,
        };
        match lock_fut.poll_unpin(ctx) {
            Poll::Pending => {
                self.lock = Some(lock_fut);
                Poll::Pending
            }
            Poll::Ready(g) => Poll::Ready(g),
        }
    }

    /// Finish locking. This should be used instead of the Mutex.lock function *if* there
    /// is a `MutexTicket` constructed already - it may be that said `MutexTicket` has already been
    /// granted ownership of the Mutex - if this is the case, the Mutex.lock call will never succeed.
    pub async fn lock(&mut self) -> MutexGuard<'a, T> {
        match self.lock.take() {
            None => self.mutex.lock(),
            Some(lock_fut) => lock_fut,
        }
        .await
    }
}

#[cfg(test)]
mod tests {

    use super::MutexTicket;
    use anyhow::{format_err, Error};
    use assert_matches::assert_matches;
    use fuchsia_async::Timer;
    use futures::{
        channel::oneshot,
        future::{poll_fn, try_join},
        lock::Mutex,
        task::noop_waker_ref,
    };
    use std::{
        task::{Context, Poll},
        time::Duration,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn basics(run: usize) {
        let mutex = Mutex::new(run);
        let mut ctx = Context::from_waker(noop_waker_ref());
        let mut poll_mutex = MutexTicket::new(&mutex);
        assert_matches!(poll_mutex.poll(&mut ctx), Poll::Ready(_));
        assert_matches!(poll_mutex.poll(&mut ctx), Poll::Ready(_));
        assert_matches!(poll_mutex.poll(&mut ctx), Poll::Ready(_));
        let mutex_guard = mutex.lock().await;
        assert_matches!(poll_mutex.poll(&mut ctx), Poll::Pending);
        assert_matches!(poll_mutex.poll(&mut ctx), Poll::Pending);
        drop(mutex_guard);
        assert_matches!(poll_mutex.poll(&mut ctx), Poll::Ready(_));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn wakes_up(run: usize) -> Result<(), Error> {
        let mutex = Mutex::new(run);
        let (tx_saw_first_pending, rx_saw_first_pending) = oneshot::channel();
        let mut poll_mutex = MutexTicket::new(&mutex);
        let mutex_guard = mutex.lock().await;
        try_join(
            async move {
                assert_matches!(
                    poll_mutex.poll(&mut Context::from_waker(noop_waker_ref())),
                    Poll::Pending
                );
                tx_saw_first_pending.send(()).map_err(|_| format_err!("cancelled"))?;
                assert_eq!(*poll_fn(|ctx| poll_mutex.poll(ctx)).await, run);
                Ok(())
            },
            async move {
                rx_saw_first_pending.await?;
                Timer::new(Duration::from_millis(300)).await;
                drop(mutex_guard);
                Ok(())
            },
        )
        .await
        .map(|_| ())
    }
}
