// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module providing the Context type, which manages the life cycle of a group of asynchronous tasks
//! on a multi-threaded Fuchsia executor.

use failure::Fail;
use fuchsia_async as fasync;
use futures::select;
use futures::channel::oneshot;
use futures::future::{BoxFuture, FusedFuture, RemoteHandle, Shared};
use futures::lock::Mutex;
use futures::prelude::*;
use futures::stream::FuturesUnordered;
use std::sync::Arc;

/// ContextError is the failure type for this module.
#[derive(Debug, Fail)]
pub enum ContextError {
    /// AlreadyCancelled is returned when cancellation is in progress or cancellation is
    /// complete.
    #[fail(display = "context is already cancelled")]
    AlreadyCancelled,
}

/// Context manages spawning and gracefully terminating asynchronous tasks on a multi-threaded
/// Fuchsia executor, where individual tasks receive cancellation signals and are in control of
/// terminating themselves. This allows the context owner to await for completion of the tasks.
/// Contexts can also be nested, allowing for independent cancellation of a child Context, or
/// cancelling parent and child Contexts together (through cancelling the parent).
/// Note that a dropped Context will cancel its own tasks, but *not* its childrens' tasks. Hence,
/// it is not recommended to rely on std::ops::drop, but rather to always use cancel() prior to
/// destruction. (This might be changed in the future).
///
/// The context has three implicit states: (1) active, (2) cancellation in progress and (3)
/// complete. State (1) is reflected in the success of `spawn`, and (3) is reflected as the success
/// of the `cancel` method. The possible state transitions are (1)->(2) and (2)->(3); note that
/// once a context is cancelled new tasks can never be spawned on it again.
#[derive(Clone)]
pub struct Context {
    /// The cancellation future, which completes when the cancellation begins.
    cancel_receiver: ContextCancel,

    /// Mutable state about this context, see ContextState.
    state: Arc<Mutex<ContextState>>,
}

/// ContextCancel is a future which each task is able to poll for cancellation intent.
pub type ContextCancel = Shared<oneshot::Receiver<()>>;

/// ContextState represents the Context's mutable state.
enum ContextState {
    /// The Context is ready to spawn tasks, representing state (1).
    Active {
        /// cancel_sender is a one-time trigger, used to begin cancellation.
        cancel_sender: oneshot::Sender<()>,

        /// tasks is a collection of all tasks that have been spawned on this Context.
        tasks: FuturesUnordered<RemoteHandle<()>>,

        /// collection of all children that have been created directly from this Context.
        children: Vec<Context>,
    },

    /// The Context is cancelled, representing state (2) and (3).
    Cancelled,
}

impl Context {
    /// Create a new blank context, ready for task spawning.
    pub fn new() -> Self {
        let (sender, receiver) = oneshot::channel();
        let state = ContextState::Active {
            cancel_sender: sender,
            tasks: FuturesUnordered::new(),
            children: Vec::new(),
        };
        Self { cancel_receiver: receiver.shared(), state: Arc::new(Mutex::new(state)) }
    }

    /// Spawn a task on the Fuchsia executor, with a handle to the cancellation future for this
    /// context. Tasks are themselves responsible for polling the future, and if they do not
    /// complete as a response, the context will not be able to cancel at all, so well-behaved
    /// tasks should poll or select over the cancellation signal whenever it is able to terminate
    /// itself gracefully.
    ///
    /// If a context cancellation is (2) already in progress or (3) completed,
    /// `Err(ContextError::AlreadyCancelled)` will be returned.
    pub async fn spawn<F, Fut>(&self, f: F) -> Result<(), ContextError>
    where
        F: (FnOnce(ContextCancel) -> Fut) + Send + 'static,
        Fut: Future<Output = ()> + Send + 'static,
    {
        let mut state_lock = await!(self.state.lock());
        let state = &mut *state_lock;
        match state {
            ContextState::Cancelled => Err(ContextError::AlreadyCancelled),
            ContextState::Active { tasks, .. } => {
                // TODO(dnordstrom): Poll for and throw away already completed tasks.
                let cancel_receiver = self.cancel_receiver.clone();
                let inner_fn = f(cancel_receiver);
                let (remote, remote_handle) = inner_fn.remote_handle();
                tasks.push(remote_handle);
                fasync::spawn(remote);
                Ok(())
            }
        }
    }

    /// Cancel ongoing tasks and await for their completion. Note that all tasks that were
    /// successfully added will be driven to completion. If more tasks are attempted to be spawn
    /// during the lifetime of this method, they will be rejected with an AlreadyCancelled error.
    ///
    /// If this context has child contexts, they will be cancelled (concurrently, in no particular
    /// order) before the tasks of this context are cancelled.
    ///
    /// If a context cancellation is (2) already in progress or (3) completed,
    /// `Err(ContextError::AlreadyCancelled)` will be returned.
    pub fn cancel<'a>(&'a self) -> BoxFuture<'a, Result<(), ContextError>> {
        // Since this method is recursive, we cannot use `async fn` directly, hence the BoxFuture.
        let state = self.state.clone();
        async move {
            let state = {
                let mut state_lock = await!(state.lock());
                std::mem::replace(&mut *state_lock, ContextState::Cancelled)
            };
            match state {
                ContextState::Cancelled => Err(ContextError::AlreadyCancelled),
                ContextState::Active { cancel_sender, tasks, children } => {
                    let cancel_children: FuturesUnordered<_> =
                        children.iter().map(|child| child.cancel()).collect();
                    let _ = await!(cancel_children.collect::<Vec<_>>());
                    let _ = cancel_sender.send(());
                    await!(tasks.collect::<()>());
                    Ok(())
                }
            }
        }
            .boxed()
    }

    /// Create a child context that will be automatically cancelled when the parent context is
    /// cancelled.
    pub async fn create_child(&self) -> Result<Self, ContextError> {
        let mut state_lock = await!(self.state.lock());
        let state = &mut *state_lock;
        match state {
            ContextState::Cancelled => Err(ContextError::AlreadyCancelled),
            ContextState::Active { children, .. } => {
                let child = Self::new();
                children.push(child.clone());
                Ok(child)
            }
        }
    }
}

/// Returns a future which resolves either when cancel or fut is ready. If both are ready, cancel
/// takes precedence. If the provided future wins, its output is returned. If cancel wins, None
/// is returned.
// TODO(dnordstrom): Consider removing once there is a well-ordered select function or macro in
// futures-rs.
pub async fn cancel_or<Fut, T>(cancel: &ContextCancel, mut fut: Fut) -> Option<T>
    where Fut: Future<Output=T> + FusedFuture + std::marker::Unpin {
    let mut cancel = cancel.clone();
    if cancel.peek().is_some() {
        return None;
    }
    select! {
        _ = cancel => None,
        value = fut => Some(value),
    }
}

#[cfg(test)]
mod test {
    use super::*;

    // A context without tasks should be cancellable.
    #[fuchsia_async::run_until_stalled(test)]
    async fn empty_test() {
        let ctx = Context::new();
        assert!(await!(ctx.cancel()).is_ok());
        assert!(await!(ctx.cancel()).is_err());
    }

    // Tasks without await-points
    #[fuchsia_async::run_until_stalled(test)]
    async fn trivial_test() {
        let ctx = Context::new();
        let fut = future::ready(());
        assert!(await!(ctx.spawn(move |_cancel| fut)).is_ok());
        assert!(await!(ctx.spawn(|_cancel| async move {})).is_ok());
        assert!(await!(ctx.cancel()).is_ok());

        // Can't cancel or spawn now
        assert!(await!(ctx.spawn(|_cancel| async move {})).is_err());
        assert!(await!(ctx.cancel()).is_err());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn complete_before_cancel_test() {
        // Checks that a task completes by itself without being cancelled
        let (sender, receiver) = oneshot::channel();
        let a_task = |_cancel| {
            async {
                sender.send(10).expect("sending failed");
            }
        };
        let ctx = Context::new();
        await!(ctx.spawn(a_task)).expect("spawning failed");
        assert_eq!(await!(receiver), Ok(10));
        assert!(await!(ctx.cancel()).is_ok());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn cancel_test() {
        // Checks that a task completes by itself without being cancelled
        let (sender, receiver) = oneshot::channel();
        let a_task = |cancel: ContextCancel| {
            async {
                await!(cancel).expect("cancel signal not delivered properly");
                sender.send(10).expect("sending failed");
            }
        };
        let ctx = Context::new();
        await!(ctx.spawn(a_task)).expect("spawning failed");
        await!(ctx.cancel()).expect("cancelling failed");
        assert_eq!(await!(receiver), Ok(10));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn clone_test() {
        // Add a task to a context, clone the context, add another task through the clone and then
        // cancel through the clone.
        let (sender_1, receiver_1) = oneshot::channel();
        let (sender_2, receiver_2) = oneshot::channel();
        let task_1 = |cancel: ContextCancel| {
            async {
                await!(cancel).expect("cancel signal not delivered properly");
                sender_1.send(1).expect("sending failed");
            }
        };
        let task_2 = |cancel: ContextCancel| {
            async {
                await!(cancel).expect("cancel signal not delivered properly");
                sender_2.send(2).expect("sending failed");
            }
        };
        let ctx = Context::new();
        assert!(await!(ctx.spawn(task_1)).is_ok());
        let ctx_clone = ctx.clone();
        assert!(await!(ctx_clone.spawn(task_2)).is_ok());
        assert!(await!(ctx_clone.cancel()).is_ok());
        assert_eq!(await!(receiver_1), Ok(1));
        assert_eq!(await!(receiver_2), Ok(2));
        assert!(await!(ctx_clone.cancel()).is_err());
        assert!(await!(ctx.cancel()).is_err());
        assert!(await!(ctx.spawn(|_| future::ready(()))).is_err());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn parent_cancels_children_test() {
        // Create a parent and child context and verify that the parent cancels its children.
        let (sender_1, receiver_1) = oneshot::channel();
        let (sender_2, receiver_2) = oneshot::channel();
        let task_1 = |cancel: ContextCancel| {
            async {
                await!(cancel).expect("cancel signal not delivered properly");
                sender_1.send(1).expect("sending failed");
            }
        };
        let task_2 = |cancel: ContextCancel| {
            async {
                await!(cancel).expect("cancel signal not delivered properly");
                sender_2.send(2).expect("sending failed");
            }
        };
        let ctx_parent = Context::new();
        assert!(await!(ctx_parent.spawn(task_1)).is_ok());
        let ctx_child = await!(ctx_parent.create_child()).expect("failed creating child context");
        assert!(await!(ctx_child.spawn(task_2)).is_ok());
        assert!(await!(ctx_parent.cancel()).is_ok());
        assert_eq!(await!(receiver_1), Ok(1));
        assert_eq!(await!(receiver_2), Ok(2));
        assert!(await!(ctx_child.cancel()).is_err());
        assert!(await!(ctx_parent.spawn(|_| future::ready(()))).is_err());
        assert!(await!(ctx_child.spawn(|_| future::ready(()))).is_err());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn child_does_not_cancel_parent_test() {
        let (sender_1, receiver_1) = oneshot::channel();
        let (sender_2, receiver_2) = oneshot::channel();

        let task_1 = |cancel: ContextCancel| {
            async {
                await!(cancel).expect("cancel signal not delivered properly");
                sender_1.send(1).expect("sending failed");
            }
        };
        let task_2 = |cancel: ContextCancel| {
            async {
                await!(cancel).expect("cancel signal not delivered properly");
                sender_2.send(2).expect("sending failed");
            }
        };
        let ctx_parent = Context::new();
        assert!(await!(ctx_parent.spawn(task_1)).is_ok());
        let ctx_child = await!(ctx_parent.create_child()).expect("failed creating child context");
        assert!(await!(ctx_child.spawn(task_2)).is_ok());
        assert!(await!(ctx_child.cancel()).is_ok());
        assert_eq!(await!(receiver_2), Ok(2));
        assert!(await!(ctx_child.cancel()).is_err());

        // Verify we can create another child context.
        let ctx_child_2 = await!(ctx_parent.create_child()).expect("failed creating child context");
        assert!(await!(ctx_child.spawn(|_| future::ready(()))).is_err());
        assert!(await!(ctx_child.create_child()).is_err());

        assert!(await!(ctx_parent.cancel()).is_ok());
        assert!(await!(ctx_child_2.cancel()).is_err());
        assert!(await!(ctx_parent.create_child()).is_err());
        assert_eq!(await!(receiver_1), Ok(1));
        assert!(await!(ctx_parent.spawn(|_| future::ready(()))).is_err());
    }

    #[test]
    fn stalled_test() {
        // Checks that if a task doesn't complete, cancel stalls forever
        let mut executor = fasync::Executor::new().expect("Failed to create executor");
        let complete = |_cancel| future::ready(());
        let never_complete = |_cancel| future::empty();
        let ctx = Context::new();
        let ctx_clone = ctx.clone();
        let spawn_fut = &mut async move {
            await!(ctx_clone.spawn(complete)).expect("spawning failed");
            await!(ctx_clone.spawn(never_complete)).expect("spawning failed");
        }
            .boxed();
        let cancel_fut = &mut ctx.cancel().boxed();
        assert!(executor.run_until_stalled(spawn_fut).is_ready());
        assert!(executor.run_until_stalled(cancel_fut).is_pending());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn cancel_or_test() {
        // Check that cancel_or function resolves the correct future for common cases
        let (sender_1, receiver_1) = oneshot::channel();
        let (sender_2, receiver_2) = oneshot::channel();
        let a_task = |cancel: ContextCancel| {
            async move {
                // if fut is ready and cancel is pending, fut wins
                assert_eq!(await!(cancel_or(&cancel, future::ready(9000))), Some(9000));

                // this send triggers the cancellation
                sender_1.send(10).expect("sending failed");

                // if fut is pending, and cancel is (or soon becomes) ready, cancel wins
                assert!(await!(cancel_or(&cancel, future::empty::<i64>())).is_none());

                // if both fut and cancel is ready, cancel wins
                assert!(await!(cancel_or(&cancel, future::ready(9001))).is_none());
                sender_2.send(20).expect("sending failed");
            }
        };
        let ctx = Context::new();
        await!(ctx.spawn(a_task)).expect("spawning failed");
        assert_eq!(await!(receiver_1), Ok(10));
        await!(ctx.cancel()).expect("cancelling failed");
        assert_eq!(await!(receiver_2), Ok(20));
    }
}
