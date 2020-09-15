// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{cutex::ALWAYS_TRUE, AcquisitionPredicate, Cutex, CutexGuard, CutexLockFuture};
use futures::prelude::*;
use std::pin::Pin;
use std::task::{Context, Poll};

/// Helper to poll a cutex.
///
/// Since Cutex::lock futures keep track of where in queue the lock request is,
/// this is different to `cutex.lock().poll(ctx)` as that construction will create
/// a new lock request at each poll.
/// This can often be useful when we need to poll something that is contained under
/// this mutex.
///
/// Typical usage:
///   let mut ticket = CutexTicket::new();
///   poll_fn(|ctx| {
///     let cutex_guard = ready!(ticket.poll(ctx));
///     cutex_guard.some_child_future.poll(ctx)
///   }).await;
///
/// What this means:
///   Attempt to acquire the cutex. If it's not available, wait until it's available.
///   With the cutex acquired, check some_child_future.
///   If it's completed, complete the poll_fn.
///   *If it's not completed* drop the cutex guard (unblock other tasks) and wait for
///   some_child_future to be awoken.
pub struct CutexTicket<'a, 'b, T> {
    cutex: &'a Cutex<T>,
    check: Pin<&'b dyn AcquisitionPredicate<T>>,
    lock: Option<CutexLockFuture<'a, 'b, T>>,
}

impl<'a, 'b, T> CutexTicket<'a, 'b, T> {
    /// Construct a new `CutexTicket`
    pub fn new(cutex: &'a Cutex<T>) -> CutexTicket<'a, 'b, T> {
        Self::new_when_pinned(cutex, Pin::new(&ALWAYS_TRUE))
    }

    /// Construct a new `CutexTicket` with a predicate
    pub fn new_when_pinned(
        cutex: &'a Cutex<T>,
        check: Pin<&'b dyn AcquisitionPredicate<T>>,
    ) -> CutexTicket<'a, 'b, T> {
        CutexTicket { cutex, check, lock: None }
    }

    /// Poll once to see if the lock has been acquired.
    pub fn poll(&mut self, ctx: &mut Context<'_>) -> Poll<CutexGuard<'a, T>> {
        let mut lock_fut = match self.lock.take() {
            None => self.cutex.lock_when_pinned(self.check),
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
}

#[cfg(test)]
mod poll_cutex_test {

    use super::CutexTicket;
    use crate::Cutex;
    use anyhow::{format_err, Error};
    use fuchsia_async::Timer;
    use futures::{
        channel::oneshot,
        future::{poll_fn, try_join},
        task::noop_waker_ref,
    };
    use matches::assert_matches;
    use std::{
        task::{Context, Poll},
        time::Duration,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn basics(run: usize) {
        let cutex = Cutex::new(run);
        let mut ctx = Context::from_waker(noop_waker_ref());
        let mut poll_cutex = CutexTicket::new(&cutex);
        assert_matches!(poll_cutex.poll(&mut ctx), Poll::Ready(_));
        assert_matches!(poll_cutex.poll(&mut ctx), Poll::Ready(_));
        assert_matches!(poll_cutex.poll(&mut ctx), Poll::Ready(_));
        let cutex_guard = cutex.lock().await;
        assert_matches!(poll_cutex.poll(&mut ctx), Poll::Pending);
        assert_matches!(poll_cutex.poll(&mut ctx), Poll::Pending);
        drop(cutex_guard);
        assert_matches!(poll_cutex.poll(&mut ctx), Poll::Ready(_));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn wakes_up(run: usize) -> Result<(), Error> {
        let cutex = Cutex::new(run);
        let (tx_saw_first_pending, rx_saw_first_pending) = oneshot::channel();
        let mut poll_cutex = CutexTicket::new(&cutex);
        let cutex_guard = cutex.lock().await;
        try_join(
            async move {
                assert_matches!(
                    poll_cutex.poll(&mut Context::from_waker(noop_waker_ref())),
                    Poll::Pending
                );
                tx_saw_first_pending.send(()).map_err(|_| format_err!("cancelled"))?;
                assert_eq!(*poll_fn(|ctx| poll_cutex.poll(ctx)).await, run);
                Ok(())
            },
            async move {
                rx_saw_first_pending.await?;
                Timer::new(Duration::from_millis(300)).await;
                drop(cutex_guard);
                Ok(())
            },
        )
        .await
        .map(|_| ())
    }
}
