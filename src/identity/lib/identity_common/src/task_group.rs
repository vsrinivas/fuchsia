// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module providing the TaskGroup type, which manages the life cycle of a group of asynchronous
//! tasks on a multi-threaded Fuchsia executor.

use fuchsia_async as fasync;
use futures::channel::oneshot;
use futures::future::{BoxFuture, FusedFuture, RemoteHandle, Shared};
use futures::lock::Mutex;
use futures::prelude::*;
use futures::select;
use futures::stream::FuturesUnordered;
use std::sync::Arc;
use thiserror::Error;

/// TaskGroupError is the failure type for this module.
#[derive(Debug, Error)]
pub enum TaskGroupError {
    /// AlreadyCancelled is returned when cancellation is in progress or cancellation is complete.
    #[error("task group is already cancelled")]
    AlreadyCancelled,
}

/// TaskGroup manages spawning and gracefully terminating asynchronous tasks on a multi-threaded
/// Fuchsia executor, where individual tasks receive cancellation signals and are in control of
/// terminating themselves. This allows the TaskGroup owner to await for completion of the tasks.
/// TaskGroups can also be nested, allowing for independent cancellation of a child TaskGroup, or
/// cancelling parent and child TaskGroups together (through cancelling the parent).
/// Note that a dropped TaskGroup will cancel its own tasks, but *not* its childrens' tasks. Hence,
/// it is not recommended to rely on std::ops::drop, but rather to always use cancel() prior to
/// destruction. (This might be changed in the future).
///
/// A TaskGroup has three implicit states: (1) active, (2) cancellation in progress and (3)
/// complete. State (1) is reflected in the success of `spawn`, and (3) is reflected as the success
/// of the `cancel` method. The possible state transitions are (1)->(2) and (2)->(3); note that
/// once a TaskGroup is cancelled new tasks can never be spawned on it again.
#[derive(Clone)]
pub struct TaskGroup {
    /// The cancellation future, which completes when the cancellation begins.
    cancel_receiver: TaskGroupCancel,

    /// Mutable state about this TaskGroup, see TaskGroupState.
    state: Arc<Mutex<TaskGroupState>>,
}

/// TaskGroupCancel is a future which each task is able to poll for cancellation intent.
pub type TaskGroupCancel = Shared<oneshot::Receiver<()>>;

/// TaskGroupState represents the TaskGroup's mutable state.
enum TaskGroupState {
    /// The TaskGroup is ready to spawn tasks, representing state (1).
    Active {
        /// cancel_sender is a one-time trigger, used to begin cancellation.
        cancel_sender: oneshot::Sender<()>,

        /// tasks is a collection of all tasks that have been spawned on this TaskGroup.
        tasks: FuturesUnordered<RemoteHandle<()>>,

        /// collection of all children that have been created directly from this TaskGroup.
        children: Vec<TaskGroup>,
    },

    /// The TaskGroup is cancelled, representing state (2) and (3).
    Cancelled,
}

impl TaskGroup {
    /// Create a new blank task group, ready for task spawning.
    #[allow(clippy::new_without_default)]
    pub fn new() -> Self {
        let (sender, receiver) = oneshot::channel();
        let state = TaskGroupState::Active {
            cancel_sender: sender,
            tasks: FuturesUnordered::new(),
            children: Vec::new(),
        };
        Self { cancel_receiver: receiver.shared(), state: Arc::new(Mutex::new(state)) }
    }

    /// Spawn a task on the Fuchsia executor, with a handle to the cancellation future for this
    /// TaskGroup. Tasks are themselves responsible for polling the future, and if they do not
    /// complete as a response, the TaskGroup will not be able to cancel at all, so well-behaved
    /// tasks should poll or select over the cancellation signal whenever it is able to terminate
    /// itself gracefully.
    ///
    /// If a TaskGroup cancellation is (2) already in progress or (3) completed,
    /// `Err(TaskGroupError::AlreadyCancelled)` will be returned.
    pub async fn spawn<F, Fut>(&self, f: F) -> Result<(), TaskGroupError>
    where
        F: (FnOnce(TaskGroupCancel) -> Fut) + Send + 'static,
        Fut: Future<Output = ()> + Send + 'static,
    {
        let mut state_lock = self.state.lock().await;
        let state = &mut *state_lock;
        match state {
            TaskGroupState::Cancelled => Err(TaskGroupError::AlreadyCancelled),
            TaskGroupState::Active { tasks, .. } => {
                // TODO(dnordstrom): Poll for and throw away already completed tasks.
                let cancel_receiver = self.cancel_receiver.clone();
                let inner_fn = f(cancel_receiver);
                let (remote, remote_handle) = inner_fn.remote_handle();
                tasks.push(remote_handle);
                fasync::Task::spawn(remote).detach();
                Ok(())
            }
        }
    }

    /// Cancel ongoing tasks and await for their completion. Note that all tasks that were
    /// successfully added will be driven to completion. If more tasks are attempted to be spawn
    /// during the lifetime of this method, they will be rejected with an AlreadyCancelled error.
    ///
    /// If this TaskGroup has children, they will be cancelled (concurrently, in no particular
    /// order) before the tasks of this TaskGroup are cancelled.
    ///
    /// If a TaskGroup cancellation is (2) already in progress or (3) completed,
    /// `Err(TaskGroupError::AlreadyCancelled)` will be returned.
    pub fn cancel(&self) -> BoxFuture<'_, Result<(), TaskGroupError>> {
        // Since this method is recursive, we cannot use `async fn` directly, hence the BoxFuture.
        let state = self.state.clone();
        async move {
            let state = {
                let mut state_lock = state.lock().await;
                std::mem::replace(&mut *state_lock, TaskGroupState::Cancelled)
            };
            match state {
                TaskGroupState::Cancelled => Err(TaskGroupError::AlreadyCancelled),
                TaskGroupState::Active { cancel_sender, tasks, children } => {
                    let cancel_children: FuturesUnordered<_> =
                        children.iter().map(|child| child.cancel()).collect();
                    let _ = cancel_children.collect::<Vec<_>>().await;
                    let _ = cancel_sender.send(());
                    tasks.collect::<()>().await;
                    Ok(())
                }
            }
        }
        .boxed()
    }

    /// Cancel ongoing tasks, but do not wait for their completion. Tasks are allowed to continue
    /// running to respond to the cancelation request. If more tasks are attempted to
    /// be spawn during the lifetime of this method, they will be rejected with an AlreadyCancelled
    /// error.
    ///
    /// If this TaskGroup has children, they will be cancelled (concurrently, in no particular
    /// order) before the tasks of this TaskGroup are cancelled.
    ///
    /// If a TaskGroup cancellation is (2) already in progress or (3) completed,
    /// `Err(TaskGroupError::AlreadyCancelled)` will be returned.
    pub fn cancel_no_wait(&self) -> BoxFuture<'_, Result<(), TaskGroupError>> {
        // Since this method is recursive, we cannot use `async fn` directly, hence the BoxFuture.
        let state = self.state.clone();
        async move {
            let state = {
                let mut state_lock = state.lock().await;
                std::mem::replace(&mut *state_lock, TaskGroupState::Cancelled)
            };
            match state {
                TaskGroupState::Cancelled => Err(TaskGroupError::AlreadyCancelled),
                TaskGroupState::Active { cancel_sender, tasks, children } => {
                    children.iter().for_each(|child| {
                        let _ = child.cancel_no_wait();
                    });
                    let _ = cancel_sender.send(());
                    // Forget the remote handle, allowing the tasks to continue running after
                    // `tasks` is dropped.
                    let () = tasks.into_iter().map(RemoteHandle::forget).collect();
                    Ok(())
                }
            }
        }
        .boxed()
    }

    /// Create a child TaskGroup that will be automatically cancelled when the parent is cancelled.
    pub async fn create_child(&self) -> Result<Self, TaskGroupError> {
        let mut state_lock = self.state.lock().await;
        let state = &mut *state_lock;
        match state {
            TaskGroupState::Cancelled => Err(TaskGroupError::AlreadyCancelled),
            TaskGroupState::Active { children, .. } => {
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
pub async fn cancel_or<Fut, T>(cancel: &TaskGroupCancel, mut fut: Fut) -> Option<T>
where
    Fut: Future<Output = T> + FusedFuture + std::marker::Unpin,
{
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

    // A TaskGroup without tasks should be cancellable.
    #[fuchsia_async::run_until_stalled(test)]
    async fn empty_test() {
        let tg = TaskGroup::new();
        assert!(tg.cancel().await.is_ok());
        assert!(tg.cancel().await.is_err());
    }

    // Tasks without await-points
    #[fuchsia_async::run_until_stalled(test)]
    async fn trivial_test() {
        let tg = TaskGroup::new();
        let fut = future::ready(());
        assert!(tg.spawn(move |_cancel| fut).await.is_ok());
        assert!(tg.spawn(|_cancel| async move {}).await.is_ok());
        assert!(tg.cancel().await.is_ok());

        // Can't cancel or spawn now
        assert!(tg.spawn(|_cancel| async move {}).await.is_err());
        assert!(tg.cancel().await.is_err());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn complete_before_cancel_test() {
        // Checks that a task completes by itself without being cancelled
        let (sender, receiver) = oneshot::channel();
        let a_task = |_cancel| async {
            sender.send(10).expect("sending failed");
        };
        let tg = TaskGroup::new();
        tg.spawn(a_task).await.expect("spawning failed");
        assert_eq!(receiver.await, Ok(10));
        assert!(tg.cancel().await.is_ok());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn cancel_test() {
        // Checks that a task completes by itself without being cancelled
        let (sender, receiver) = oneshot::channel();
        let a_task = |cancel: TaskGroupCancel| async {
            cancel.await.expect("cancel signal not delivered properly");
            sender.send(10).expect("sending failed");
        };
        let tg = TaskGroup::new();
        tg.spawn(a_task).await.expect("spawning failed");
        tg.cancel().await.expect("cancelling failed");
        assert_eq!(receiver.await, Ok(10));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn cancel_no_wait_test() {
        // Checks that a task is canceled correctly.
        let (sender, receiver) = oneshot::channel();
        let a_task = |cancel: TaskGroupCancel| async {
            cancel.await.expect("cancel signal not delivered properly");
            sender.send(10).expect("sending failed");
        };
        let tg = TaskGroup::new();
        tg.spawn(a_task).await.expect("spawning failed");
        tg.cancel_no_wait().await.expect("cancelling failed");
        assert_eq!(receiver.await, Ok(10));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn clone_test() {
        // Add a task to a task group, clone it, add another task through the cloned task group and
        // finally cancel through the cloned task group.
        let (sender_1, receiver_1) = oneshot::channel();
        let (sender_2, receiver_2) = oneshot::channel();
        let task_1 = |cancel: TaskGroupCancel| async {
            cancel.await.expect("cancel signal not delivered properly");
            sender_1.send(1).expect("sending failed");
        };
        let task_2 = |cancel: TaskGroupCancel| async {
            cancel.await.expect("cancel signal not delivered properly");
            sender_2.send(2).expect("sending failed");
        };
        let tg = TaskGroup::new();
        assert!(tg.spawn(task_1).await.is_ok());
        let tg_clone = tg.clone();
        assert!(tg_clone.spawn(task_2).await.is_ok());
        assert!(tg_clone.cancel().await.is_ok());
        assert_eq!(receiver_1.await, Ok(1));
        assert_eq!(receiver_2.await, Ok(2));
        assert!(tg_clone.cancel().await.is_err());
        assert!(tg.cancel().await.is_err());
        assert!(tg.spawn(|_| future::ready(())).await.is_err());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn parent_cancels_children_test() {
        // Create a child task group and cancel the parent.
        let (sender_1, receiver_1) = oneshot::channel();
        let (sender_2, receiver_2) = oneshot::channel();
        let task_1 = |cancel: TaskGroupCancel| async {
            cancel.await.expect("cancel signal not delivered properly");
            sender_1.send(1).expect("sending failed");
        };
        let task_2 = |cancel: TaskGroupCancel| async {
            cancel.await.expect("cancel signal not delivered properly");
            sender_2.send(2).expect("sending failed");
        };
        let tg_parent = TaskGroup::new();
        assert!(tg_parent.spawn(task_1).await.is_ok());
        let tg_child = tg_parent.create_child().await.expect("failed creating child task group");
        assert!(tg_child.spawn(task_2).await.is_ok());
        assert!(tg_parent.cancel().await.is_ok());
        assert_eq!(receiver_1.await, Ok(1));
        assert_eq!(receiver_2.await, Ok(2));
        assert!(tg_child.cancel().await.is_err());
        assert!(tg_parent.spawn(|_| future::ready(())).await.is_err());
        assert!(tg_child.spawn(|_| future::ready(())).await.is_err());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn child_does_not_cancel_parent_test() {
        let (sender_1, receiver_1) = oneshot::channel();
        let (sender_2, receiver_2) = oneshot::channel();

        let task_1 = |cancel: TaskGroupCancel| async {
            cancel.await.expect("cancel signal not delivered properly");
            sender_1.send(1).expect("sending failed");
        };
        let task_2 = |cancel: TaskGroupCancel| async {
            cancel.await.expect("cancel signal not delivered properly");
            sender_2.send(2).expect("sending failed");
        };
        let tg_parent = TaskGroup::new();
        assert!(tg_parent.spawn(task_1).await.is_ok());
        let tg_child = tg_parent.create_child().await.expect("failed creating child task group");
        assert!(tg_child.spawn(task_2).await.is_ok());
        assert!(tg_child.cancel().await.is_ok());
        assert_eq!(receiver_2.await, Ok(2));
        assert!(tg_child.cancel().await.is_err());

        // Verify we can create another child task group.
        let tg_child_2 = tg_parent.create_child().await.expect("failed creating child task group");
        assert!(tg_child.spawn(|_| future::ready(())).await.is_err());
        assert!(tg_child.create_child().await.is_err());

        assert!(tg_parent.cancel().await.is_ok());
        assert!(tg_child_2.cancel().await.is_err());
        assert!(tg_parent.create_child().await.is_err());
        assert_eq!(receiver_1.await, Ok(1));
        assert!(tg_parent.spawn(|_| future::ready(())).await.is_err());
    }

    #[test]
    fn stalled_test() {
        // Checks that if a task doesn't complete, cancel stalls forever
        let mut executor = fasync::TestExecutor::new().expect("Failed to create executor");
        let complete = |_cancel| future::ready(());
        let never_complete = |_cancel| future::pending();
        let tg = TaskGroup::new();
        let tg_clone = tg.clone();
        let spawn_fut = &mut async move {
            tg_clone.spawn(complete).await.expect("spawning failed");
            tg_clone.spawn(never_complete).await.expect("spawning failed");
        }
        .boxed();
        let cancel_fut = &mut tg.cancel().boxed();
        assert!(executor.run_until_stalled(spawn_fut).is_ready());
        assert!(executor.run_until_stalled(cancel_fut).is_pending());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn cancel_or_test() {
        // Check that cancel_or function resolves the correct future for common cases
        let (sender_1, receiver_1) = oneshot::channel();
        let (sender_2, receiver_2) = oneshot::channel();
        let a_task = |cancel: TaskGroupCancel| {
            async move {
                // if fut is ready and cancel is pending, fut wins
                assert_eq!(cancel_or(&cancel, future::ready(9000)).await, Some(9000));

                // this send triggers the cancellation
                sender_1.send(10).expect("sending failed");

                // if fut is pending, and cancel is (or soon becomes) ready, cancel wins
                assert!(cancel_or(&cancel, future::pending::<i64>()).await.is_none());

                // if both fut and cancel is ready, cancel wins
                assert!(cancel_or(&cancel, future::ready(9001)).await.is_none());
                sender_2.send(20).expect("sending failed");
            }
        };
        let tg = TaskGroup::new();
        tg.spawn(a_task).await.expect("spawning failed");
        assert_eq!(receiver_1.await, Ok(10));
        tg.cancel().await.expect("cancelling failed");
        assert_eq!(receiver_2.await, Ok(20));
    }
}
