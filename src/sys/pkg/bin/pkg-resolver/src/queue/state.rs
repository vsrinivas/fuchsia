// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{Closed, TryMerge},
    futures::{
        channel::oneshot,
        future::{Either, Ready, Shared},
        prelude::*,
    },
    pin_project::pin_project,
    std::{
        collections::VecDeque,
        pin::Pin,
        task::{Context, Poll},
    },
};

// With feature(type_alias_impl_trait), boxing the future would not be necessary, which would also
// remove the requirements that O and E are Send + 'static.
#[pin_project]
pub(crate) struct TaskFuture<O> {
    #[pin]
    fut: Either<oneshot::Receiver<O>, Ready<Result<O, oneshot::Canceled>>>,
}

impl<O> Future for TaskFuture<O> {
    type Output = Result<O, Closed>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        match self.project().fut.poll(cx) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(res) => Poll::Ready(res.map_err(|oneshot::Canceled| Closed)),
        }
    }
}

/// Shared state for a pending work item. Contains the context parameter to be provided to the task
/// when running it and other metadata present when running the task.
pub struct PendingWorkInfo<C, O> {
    context: C,
    running: RunningWorkInfo<O>,
}

/// Shared state for a single running work item. Contains the sending end of the shared future for sending
/// the result of the task and the clonable shared future that will resolve to that result.
pub struct RunningWorkInfo<O> {
    cb: oneshot::Sender<O>,
    fut: Shared<TaskFuture<O>>,
}

/// Metadata about pending and running instances of a task.
pub struct TaskVariants<C, O> {
    running: Option<RunningWorkInfo<O>>,
    pending: VecDeque<PendingWorkInfo<C, O>>,
}

impl<C, O> TaskVariants<C, O>
where
    C: TryMerge,
    O: Clone,
{
    /// Creates a new TaskVariants with a single, pending instance of this task, returning Self and
    /// the completion future for the initial instance of the task.
    pub(crate) fn new(context: C) -> (Self, Shared<TaskFuture<O>>) {
        let mut res = Self { running: None, pending: VecDeque::new() };
        let fut = res.push_back(context);
        (res, fut)
    }

    /// Attempts to merge the given context with an existing pending instance of this task, or
    /// enqueues a new instance of this task with the given context. Returns the completion future
    /// for the instance of the task.
    pub(crate) fn push(&mut self, mut context: C) -> Shared<TaskFuture<O>> {
        // First try to merge this task with another queued or running task.
        for info in self.pending.iter_mut() {
            if let Err(unmerged) = info.context.try_merge(context) {
                context = unmerged;
            } else {
                return info.running.fut.clone();
            }
        }

        // Otherwise, enqueue a new task.
        self.push_back(context)
    }

    fn push_back(&mut self, context: C) -> Shared<TaskFuture<O>> {
        let (sender, fut) = make_broadcast_pair();

        self.pending.push_back(PendingWorkInfo {
            context,
            running: RunningWorkInfo { cb: sender, fut: fut.clone() },
        });
        fut
    }
}

impl<C, O> TaskVariants<C, O> {
    /// Starts the first instance of this task, claiming its context.
    ///
    /// # Panics
    ///
    /// Panics if the task has already been started.
    pub(crate) fn start(&mut self) -> C {
        self.try_start().expect("context to not yet be claimed")
    }

    /// Completes the running instance of this task, notifying waiters of the result and returning
    /// the context for the next instance of this task, if one exists.
    ///
    /// # Panics
    ///
    /// Panics if this method has previously returned `None`.
    pub(crate) fn done(&mut self, res: O) -> Option<C> {
        let cb = self.running.take().expect("running item to mark done").cb;

        // As the shared future was just removed from the running task, if all clones of that
        // future have also been dropped, this send can fail. Silently ignore that error.
        let _ = cb.send(res);

        // If further work for this key is queued, take and return its context.
        self.try_start()
    }

    fn try_start(&mut self) -> Option<C> {
        assert!(self.running.is_none());
        if let Some(PendingWorkInfo { context, running }) = self.pending.pop_front() {
            self.running = Some(running);
            Some(context)
        } else {
            None
        }
    }
}

/// Creates a sender and clonable receiver channel pair, where the receiver maps Canceled errors to
/// a [crate::TaskError].
pub(crate) fn make_broadcast_pair<O>() -> (oneshot::Sender<O>, Shared<TaskFuture<O>>)
where
    O: Clone,
{
    let (sender, receiver) = oneshot::channel();
    let fut = TaskFuture { fut: Either::Left(receiver) }.shared();

    (sender, fut)
}

/// Creates a clonable receiver of the same type as the receiving end of a broadcast receiver that
/// always reports that the task was canceled.
pub(crate) fn make_canceled_receiver<O>() -> Shared<TaskFuture<O>>
where
    O: Clone,
{
    TaskFuture { fut: Either::Right(futures::future::err(oneshot::Canceled)) }.shared()
}

#[cfg(test)]
mod tests {
    use {
        super::{super::tests::MergeEqual, *},
        futures::executor::block_on,
    };

    #[test]
    fn merges() {
        let (mut infos, fut0_a) = TaskVariants::<MergeEqual, i32>::new(MergeEqual(0));

        let fut0_b = infos.push(MergeEqual(0));
        let fut1 = infos.push(MergeEqual(1));
        let fut0_c = infos.push(MergeEqual(0));
        let fut2 = infos.push(MergeEqual(2));

        // Start the first task. Start a dup of the first task that won't be merged.
        assert_eq!(infos.start(), MergeEqual(0));
        let fut0_d = infos.push(MergeEqual(0));

        // Complete the first instance and verify futures resolve.
        assert_eq!(infos.done(0), Some(MergeEqual(1)));
        assert_eq!(block_on(fut0_a), Ok(0));
        assert_eq!(block_on(fut0_b), Ok(0));
        assert_eq!(block_on(fut0_c), Ok(0));

        // Completing the second instance starts the third.
        assert_eq!(infos.done(1), Some(MergeEqual(2)));
        assert_eq!(block_on(fut1), Ok(1));

        // Completing the third instance starts the unmerged dup of the first.
        assert_eq!(infos.done(2), Some(MergeEqual(0)));
        assert_eq!(block_on(fut2), Ok(2));

        // The unmerged dup resolves with the result here, and no work is left.
        assert_eq!(infos.done(3), None);
        assert_eq!(block_on(fut0_d), Ok(3));
    }

    #[test]
    fn task_variants() {
        let (mut infos, fut1) = TaskVariants::<(), ()>::new(());
        let fut2 = infos.push(());

        let () = infos.start();
        assert_eq!(infos.done(()), None);

        block_on(async move {
            assert_eq!(fut1.await, Ok(()));
            assert_eq!(fut2.await, Ok(()));
        });
    }
}
