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
    failure::{format_err, Error},
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_zircon as zx,
    futures::{future::FutureObj, FutureExt},
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
    ) -> FutureObj<'_, Result<Self::State, Error>>;
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
    ) -> FutureObj<'_, Result<Self::State, Error>> {
        let msg = expectation.describe();
        FutureObj::new(Box::pin(
            ExpectationFuture::new(self.clone(), expectation)
                .map(|s| Ok(s))
                .on_timeout(timeout.after_now(), move || {
                    Err(format_err!("Timed out waiting for expectation: {}", msg))
                }),
        ))
    }
}

/// Shared Harness for awaiting predicate expectations on type `S`, using
/// auxilliary data `A`
///
/// This type is the easiest way to get an implementation of 'ExpectableState'
/// to await upon. The Aux type `A` is commonly used to hold a FIDL Proxy to
/// drive the behavior under test.
pub struct ExpectationHarness<S, A>(Arc<RwLock<HarnessInner<S, A>>>);

impl<S, A> Clone for ExpectationHarness<S, A> {
    fn clone(&self) -> ExpectationHarness<S, A> {
        ExpectationHarness(self.0.clone())
    }
}

/// Harness for State `S` and Auxilliary data `A`
struct HarnessInner<S, A> {
    /// Snapshot of current state
    state: S,

    /// All the tasks currently pending on a state change
    tasks: Slab<task::Waker>,

    /// Arbitrary auxilliary data. Commonly used to hold a FIDL proxy, but can
    /// be used to store any data necessary for driving the behavior that will
    /// result in state updates.
    aux: A,
}

impl<S: Clone + 'static, A> ExpectableState for ExpectationHarness<S, A> {
    type State = S;

    /// Register a task as needing waking when state changes
    fn store_task(&mut self, cx: &mut task::Context<'_>) -> usize {
        self.0.write().tasks.insert(cx.waker().clone())
    }

    /// Remove a task from being tracked
    fn remove_task(&mut self, key: usize) {
        let mut harness = self.0.write();
        if harness.tasks.contains(key) {
            harness.tasks.remove(key);
        }
    }

    /// Notify all pending tasks that state has changed
    fn notify_state_changed(&self) {
        for task in &self.0.read().tasks {
            task.1.wake_by_ref();
        }
        self.0.write().tasks.clear()
    }

    fn read(&self) -> Self::State {
        self.0.read().state.clone()
    }
}

impl<S, A> ExpectationHarness<S, A> {
    pub fn init(aux: A, state: S) -> ExpectationHarness<S, A> {
        ExpectationHarness(Arc::new(RwLock::new(HarnessInner { aux, tasks: Slab::new(), state })))
    }

    pub fn aux(&self) -> MappedRwLockWriteGuard<'_, A> {
        RwLockWriteGuard::map(self.0.write(), |harness| &mut harness.aux)
    }

    pub fn write_state(&self) -> MappedRwLockWriteGuard<'_, S> {
        RwLockWriteGuard::map(self.0.write(), |harness| &mut harness.state)
    }
}

impl<S: Default, A> ExpectationHarness<S, A> {
    pub fn new(aux: A) -> ExpectationHarness<S, A> {
        ExpectationHarness::<S, A>::init(aux, S::default())
    }
}
