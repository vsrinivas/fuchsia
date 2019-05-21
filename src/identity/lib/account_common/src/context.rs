// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module providing the Context type, which manages the life cycle of a group of asynchronous tasks
//! on a multi-threaded Fuchsia executor.

use failure::Fail;
use fuchsia_async as fasync;
use futures::channel::oneshot;
use futures::future::{RemoteHandle, Shared};
use futures::lock::Mutex;
use futures::prelude::*;
use futures::stream::FuturesUnordered;

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
/// Note that a dropped context will cancel its tasks.
///
/// The context has three implicit states: (1) active, (2) cancellation in progress and (3)
/// complete. State (1) is reflected in the success of `spawn`, and (3) is reflected as the success
/// of the `cancel` method. The possible state transitions are (1)->(2) and (2)->(3); note that
/// once a context is cancelled new tasks can never be spawned on it again.
pub struct Context {
    /// The cancellation future, which completes when the cancellation begins.
    cancel_receiver: ContextCancel,

    /// Mutable state about this context, see ContextState.
    state: Mutex<ContextState>,
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
    },

    /// The Context is cancelled, representing state (2) and (3).
    Cancelled,
}

impl Context {
    /// Create a new blank context, ready for task spawning.
    pub fn new() -> Self {
        let (sender, receiver) = oneshot::channel();
        let state = ContextState::Active { cancel_sender: sender, tasks: FuturesUnordered::new() };
        Self { cancel_receiver: receiver.shared(), state: Mutex::new(state) }
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
            ContextState::Active { cancel_sender: _, tasks } => {
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
    /// If a context cancellation is (2) already in progress or (3) completed,
    /// `Err(ContextError::AlreadyCancelled)` will be returned.
    pub async fn cancel(&self) -> Result<(), ContextError> {
        let state = {
            let mut state_lock = await!(self.state.lock());
            std::mem::replace(&mut *state_lock, ContextState::Cancelled)
        };
        match state {
            ContextState::Cancelled => Err(ContextError::AlreadyCancelled),
            ContextState::Active { cancel_sender, tasks } => {
                let _ = cancel_sender.send(());
                await!(tasks.collect::<()>());
                Ok(())
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::sync::Arc;

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

    #[test]
    fn stalled_test() {
        // Checks that if a task doesn't complete, cancel stalls forever
        let mut executor = fasync::Executor::new().expect("Failed to create executor");
        let complete = |_cancel| future::ready(());
        let never_complete = |_cancel| future::empty();
        let ctx = Arc::new(Context::new());
        let ctx_clone = Arc::clone(&ctx);
        let spawn_fut = &mut async move {
            await!(ctx_clone.spawn(complete)).expect("spawning failed");
            await!(ctx_clone.spawn(never_complete)).expect("spawning failed");
        }
            .boxed();
        let cancel_fut = &mut ctx.cancel().boxed();
        assert!(executor.run_until_stalled(spawn_fut).is_ready());
        assert!(executor.run_until_stalled(cancel_fut).is_pending());
    }
}
