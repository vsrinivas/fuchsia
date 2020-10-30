// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module defines a framework for using the crate::expectations module in
//! asynchronous situations, where you wish you detect the satisfiability of the
//! expectations at some undetermined point in the future.
//!
//! An `ExpectationFuture` implements `Future` and will complete when the
//! underlying state satisfies the predicate. Failure can be detected by wrapping
//! the future in a timeout, as is implemented by the method `when_satisfied`.
//!
//! To use `ExpectationFuture`s, you must implement `ExpectableState` for the
//! type of state you wish to track, which defines how the state will update and
//! notify tasks that are waiting on state changes.
//!
//! A common pattern is to await the expectation of a given state, and then
//! check further predicate expectations to determine success or failure at that
//! point in time.
//!
//! e.g.
//!
//!   ```ignore
//!   // Wait for the action to have completed one way or the other
//!   let state = state.when_satisfied(action_complete, timeout).await?;
//!   // Then check that the action completed successfully
//!   action_success.satisfied(state)
//!   ```

use {
    anyhow::{format_err, Error},
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_zircon as zx,
    futures::{future::BoxFuture, FutureExt},
    parking_lot::{MappedRwLockWriteGuard, RwLock, RwLockWriteGuard},
    slab::Slab,
    std::{
        future::Future,
        pin::Pin,
        sync::Arc,
        task::{self, Poll},
    },
};

use crate::expectation::Predicate;

/// Future that completes once a Predicate is satisfied for the `T::State` type
/// where `T` is some type that allows monitoring of State updates
pub struct ExpectationFuture<T: ExpectableState> {
    state: T,
    expectation: Predicate<T::State>,
    waker_key: Option<usize>,
}

impl<T: ExpectableState> ExpectationFuture<T> {
    fn new(state: T, expectation: Predicate<T::State>) -> ExpectationFuture<T> {
        ExpectationFuture { state, expectation, waker_key: None }
    }

    fn clear_waker(&mut self) {
        if let Some(key) = self.waker_key {
            self.state.remove_task(key);
            self.waker_key = None;
        }
    }

    fn store_task(&mut self, cx: &mut task::Context<'_>) {
        let key = self.state.store_task(cx);
        self.waker_key = Some(key);
    }
}

impl<T: ExpectableState> std::marker::Unpin for ExpectationFuture<T> {}

#[must_use = "futures do nothing unless polled"]
impl<T: ExpectableState> Future for ExpectationFuture<T> {
    type Output = T::State;

    fn poll(mut self: Pin<&mut Self>, cx: &mut task::Context<'_>) -> Poll<Self::Output> {
        self.clear_waker();
        let state = self.state.read();
        if self.expectation.satisfied(&state) {
            Poll::Ready(state)
        } else {
            self.store_task(cx);
            Poll::Pending
        }
    }
}

/// Trait for objects that allow futures to trigger based on state changes
///
/// This trait will commonly be used via the `ExpectableStateExt` extension
/// trait which provides the convenient `when_satisfied` function.
///
/// You can implement this trait for your own types which implement their own
/// state tracking and notification of futures, or you can use the
/// `ExpectationHarness` struct in this module which provides a ready to use
/// implementation.
pub trait ExpectableState: Clone {
    /// Type of current state we are tracking
    type State: 'static;

    /// Register a task as needing waking when state changes
    fn store_task(&mut self, cx: &mut task::Context<'_>) -> usize;

    /// Remove a task from being tracked. Called by `ExpectationFuture` when
    /// polled.
    fn remove_task(&mut self, key: usize);

    /// Notify all pending tasks that state has changed
    fn notify_state_changed(&self);

    /// Read a snapshot of the current State
    fn read(&self) -> Self::State;
}

pub trait ExpectableStateExt: ExpectableState + Sized {
    /// Convenience method for awaiting expectations on the underlying state
    /// Provides a simple syntax for asynchronously awaiting expectations:
    ///
    ///  ```ignore
    ///    // Wait for the action to have completed one way or the other
    ///    let state = state.when_satisfied(action_complete, timeout).await?;
    ///  ```
    fn when_satisfied(
        &self,
        expectation: Predicate<Self::State>,
        timeout: zx::Duration,
    ) -> BoxFuture<'_, Result<Self::State, Error>>;
}

impl<T: ExpectableState + Sized> ExpectableStateExt for T
where
    T: Send + Sync + 'static,
    T::State: Send + Sync + 'static,
{
    fn when_satisfied(
        &self,
        expectation: Predicate<T::State>,
        timeout: zx::Duration,
    ) -> BoxFuture<'_, Result<Self::State, Error>> {
        let state = self.clone();
        let exp = expectation.clone();
        ExpectationFuture::new(self.clone(), expectation)
            .map(|s| Ok(s))
            .on_timeout(timeout.after_now(), move || {
                let state = state.read();
                let result = exp.assert_satisfied(&state);
                result.map(|_| state).map_err(|err| {
                    format_err!("Timed out waiting for expectation, last result:\n{:?}", err)
                })
            })
            .boxed()
    }
}

/// Inner state for the `Expectable` helper type
pub struct ExpectableInner<S, A> {
    // Current state
    pub state: S,

    // Pending Tasks
    tasks: Slab<task::Waker>,

    // Auxillary shared data
    pub aux: A,
}

/// `Expectable<S,A>` is an easy way to build an implementation of `ExpectableState` to await upon.
/// The Aux type `A` is commonly used to hold a FIDL Proxy to drive the behavior under test.
pub type Expectable<S, A> = Arc<RwLock<ExpectableInner<S, A>>>;

pub fn expectable<S, A>(state: S, aux: A) -> Expectable<S, A> {
    Arc::new(RwLock::new(ExpectableInner { state, tasks: Slab::new(), aux }))
}

impl<S: Clone + 'static, A> ExpectableState for Expectable<S, A> {
    type State = S;

    /// Register a task as needing waking when state changes
    fn store_task(&mut self, cx: &mut task::Context<'_>) -> usize {
        self.write().tasks.insert(cx.waker().clone())
    }

    /// Remove a task from being tracked
    fn remove_task(&mut self, key: usize) {
        let mut harness = self.write();
        if harness.tasks.contains(key) {
            harness.tasks.remove(key);
        }
    }

    /// Notify all pending tasks that state has changed
    fn notify_state_changed(&self) {
        for task in &RwLock::read(self).tasks {
            task.1.wake_by_ref();
        }
        self.write().tasks.clear()
    }

    fn read(&self) -> Self::State {
        RwLock::read(self).state.clone()
    }
}

/// A trait to provide convenience methods on Expectable types held within wrapping harnesses
pub trait ExpectableExt<S, A> {
    /// Mutable access the auxilliary data
    fn aux(&self) -> MappedRwLockWriteGuard<'_, A>;

    /// Mutable access to the state
    fn write_state(&self) -> MappedRwLockWriteGuard<'_, S>;
}

// All `Expectable<S,A>` provide these methods, and thus so do any types implementing `Deref` whose
// targets are `Expectable<S,A>`
impl<S, A> ExpectableExt<S, A> for Expectable<S, A> {
    fn aux(&self) -> MappedRwLockWriteGuard<'_, A> {
        RwLockWriteGuard::map(self.write(), |harness| &mut harness.aux)
    }

    fn write_state(&self) -> MappedRwLockWriteGuard<'_, S> {
        RwLockWriteGuard::map(self.write(), |harness| &mut harness.state)
    }
}
