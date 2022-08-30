// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async::Task,
    futures::{channel::mpsc, StreamExt},
};

/// Maintains a collection of pending local [`Task`]s, allowing them to be dropped (and cancelled)
/// en masse.
#[derive(Debug)]
pub(crate) struct LocalTaskTracker {
    sender: mpsc::UnboundedSender<Task<()>>,
    _receiver_task: Task<()>,
}

impl LocalTaskTracker {
    pub fn new() -> Self {
        let (sender, receiver) = mpsc::unbounded();
        let receiver_task = Task::local(async move {
            // Drop the tasks as they are completed.
            receiver.for_each_concurrent(None, |task: Task<()>| task).await
        });

        Self { sender, _receiver_task: receiver_task }
    }

    /// Submits a new task to track.
    pub fn track(&self, task: Task<()>) {
        match self.sender.unbounded_send(task) {
            Ok(_) => {}
            // `Full` should never happen because this is unbounded.
            // `Disconnected` might happen if the `Service` was dropped. However, it's not clear how
            // to create such a race condition.
            Err(e) => tracing::error!("Unexpected {e:?} while pushing task"),
        };
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Error,
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        futures::channel::oneshot,
        std::{cell::RefCell, rc::Rc, task::Poll},
    };

    /// Makes a `Task` that waits for a `oneshot`'s value to be set, and then forwards that value to
    /// a reference-counted container that can be observed outside the task.
    fn make_signalable_task<T: Default + 'static>() -> (oneshot::Sender<T>, Task<()>, Rc<RefCell<T>>)
    {
        let (sender, receiver) = oneshot::channel();
        let task_completed = Rc::new(RefCell::new(<T as Default>::default()));
        let task_completed_ = task_completed.clone();
        let task = Task::local(async move {
            if let Ok(value) = receiver.await {
                *task_completed_.borrow_mut() = value;
            }
        });
        (sender, task, task_completed)
    }

    #[fuchsia::test]
    fn smoke_test() -> Result<(), Error> {
        let mut exec = fasync::TestExecutor::new()?;

        let (mut sender_1, task_1, completed_1) = make_signalable_task::<bool>();
        let (sender_2, task_2, completed_2) = make_signalable_task::<bool>();

        let mut tracker = LocalTaskTracker::new();

        tracker.track(task_1);
        tracker.track(task_2);

        assert_matches!(exec.run_until_stalled(&mut tracker._receiver_task), Poll::Pending);
        assert_eq!(Rc::strong_count(&completed_1), 2);
        assert_eq!(Rc::strong_count(&completed_2), 2);
        assert!(!sender_1.is_canceled());
        assert!(!sender_2.is_canceled());

        assert!(sender_2.send(true).is_ok());
        assert_matches!(exec.run_until_stalled(&mut tracker._receiver_task), Poll::Pending);

        assert_eq!(Rc::strong_count(&completed_1), 2);
        assert_eq!(Rc::strong_count(&completed_2), 1);
        assert_eq!(*completed_1.borrow(), false);
        assert_eq!(*completed_2.borrow(), true);
        assert!(!sender_1.is_canceled());

        drop(tracker);
        let mut sender_1_cancellation = sender_1.cancellation();
        assert_matches!(exec.run_until_stalled(&mut sender_1_cancellation), Poll::Ready(()));
        assert_eq!(Rc::strong_count(&completed_1), 1);
        assert!(sender_1.is_canceled());

        Ok(())
    }
}
