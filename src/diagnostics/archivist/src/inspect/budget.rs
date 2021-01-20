// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::Mutex;
use std::{
    future::Future,
    pin::Pin,
    sync::Arc,
    task::{Context, Poll, Waker},
};

/// A `SnapshotBudget` acts as a concurrency controller for inspect snapshots. In practice, its
/// implementation amounts to an async semaphore.
///
/// `Clone`ing the budget results in handles which share the same underlying budget. Multiple
/// budgets can be simultaneously managed, although in practice a single budget is created in the
/// constructor for `DataRepo` and we don't run multiple of those.
#[derive(Clone)]
pub struct SnapshotBudget {
    state: Arc<Mutex<BudgetState>>,
}

impl SnapshotBudget {
    /// Create a new budget with `max_concurrent_snapshots`.
    pub fn new(max_concurrent_snapshots: usize) -> Self {
        assert!(max_concurrent_snapshots > 0, "must allow 1 or more snapshots concurrently.");
        Self {
            state: Arc::new(Mutex::new(BudgetState {
                num_in_flight: 0,
                max_in_flight: max_concurrent_snapshots as u64,
                wakers: Default::default(),
            })),
        }
    }

    /// Returns a future which completes when it is the caller's turn to use a slot in the budget.
    pub fn acquire(&self) -> impl Future<Output = SnapshotAllowance> {
        PendingAllowance { state: self.state.clone() }
    }
}

struct BudgetState {
    /// The number of `SnapshotAllowances` currently live.
    num_in_flight: u64,
    /// The maximum number of `SnapshotAllowances` we should permit.
    max_in_flight: u64,
    /// Wakers for all of the `PendingAllowance`s waiting for their turn.
    wakers: Vec<Waker>,
}

struct PendingAllowance {
    state: Arc<Mutex<BudgetState>>,
}

impl Future for PendingAllowance {
    type Output = SnapshotAllowance;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let mut state = self.state.lock();

        if state.num_in_flight < state.max_in_flight {
            state.num_in_flight += 1;
            drop(state);
            Poll::Ready(SnapshotAllowance { state: self.state.clone() })
        } else {
            state.wakers.push(cx.waker().clone());
            Poll::Pending
        }
    }
}

/// A token which represents that the holder is allowed to proceed with snapshotting.
///
/// Note: Calling `mem::forget()` on this value or leaking it via an Arc-cycle is a serious logic
/// error and may result in a deadlocked Archivist.
pub struct SnapshotAllowance {
    state: Arc<Mutex<BudgetState>>,
}

impl Drop for SnapshotAllowance {
    fn drop(&mut self) {
        let mut state = self.state.lock();
        state.num_in_flight -= 1;

        // we wake all wakers even though in most situations we only need to wake one. this buys us
        // some simplicity on two axes:
        //   1. we don't need to reason about canceled waiters
        //   2. we don't need to manage IDs to track wakers for determining who to wake
        //
        // this comes at the expense of some additional allocations, but the `wakers` vec will
        // eventually hit a high-water mark for its size and the allocations will be reused from
        // then on.
        //
        // it also comes at the expense of some additional wakeups, but wakeups are cheap
        // and such is life.
        state.wakers.drain(..).for_each(|w| w.wake());
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::FutureExt;

    #[test]
    fn snapshot_budget_instantly_succeeds_when_empty() {
        SnapshotBudget::new(1).acquire().now_or_never().unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn snapshot_budget_pauses_next_waiter_when_full() {
        let count = 10;
        let budget = SnapshotBudget::new(count);
        let mut in_progress = vec![];

        for _ in 0..count {
            in_progress.push(budget.acquire().await);
        }

        let mut paused = budget.acquire();
        assert!(futures::poll!(&mut paused).is_pending());
        drop(in_progress);
        assert!(futures::poll!(&mut paused).is_ready());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn waiting_task_is_woken() {
        let budget = SnapshotBudget::new(1);
        let in_progress = budget.acquire().await;

        let paused = budget.acquire();
        let mut spawned = fuchsia_async::Task::spawn(async move {
            paused.await;
            1
        });

        assert!(futures::poll!(&mut spawned).is_pending());
        drop(in_progress);

        println!("{}", spawned.await); // use the output so rustc is happy
    }
}
