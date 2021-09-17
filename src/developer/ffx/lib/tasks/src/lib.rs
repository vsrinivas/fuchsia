// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines some basic task management structures that behave like
//! [futures::stream::futures_unordered::FuturesUnordered], but without the
//! need to poll to make forward progress.
//!
//! Useful for cases where detaching a [fuchsia_async::Task] is either not
//! viable or not advised. Tasks spawned using [TaskManager] are auto-cleaned
//! up from the manager after completion.
//!
//! For shutdown, the [TaskManager] can be drained to get all instances of
//! running tasks for either joining them or drop them.

use {
    fuchsia_async::futures::future::ready,
    fuchsia_async::futures::FutureExt,
    fuchsia_async::Task,
    std::cell::{Cell, RefCell},
    std::collections::HashMap,
    std::future::Future,
    std::num::Wrapping,
    std::rc::Rc,
};

#[derive(Debug, Default)]
pub struct TaskManager {
    next_task_id: Cell<Wrapping<usize>>,
    inner: Rc<RefCell<HashMap<usize, Task<()>>>>,
}

impl TaskManager {
    pub fn new() -> Self {
        Default::default()
    }

    pub fn spawn(&self, fut: impl Future<Output = ()> + 'static) {
        let task_id = self.next_task_id.get();
        self.next_task_id.set(task_id + Wrapping(1usize));
        let task_id = task_id.0;

        let tasks = self.inner.clone();
        let fut = fut.then(move |_| {
            tasks.borrow_mut().remove(&task_id);
            ready(())
        });
        self.inner.borrow_mut().insert(task_id, Task::local(fut));
    }

    pub fn drain(&self) -> Vec<Task<()>> {
        self.inner.borrow_mut().drain().map(|(_, v)| v).collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_drain() {
        let t = TaskManager::new();
        t.spawn(fuchsia_async::futures::future::pending());
        let v = t.drain();
        assert_eq!(v.len(), 1);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_cleanup() {
        let t = TaskManager::new();
        t.spawn(fuchsia_async::futures::future::ready(()));
        t.spawn(fuchsia_async::futures::future::ready(()));
        t.spawn(fuchsia_async::futures::future::ready(()));
        fuchsia_async::Timer::new(std::time::Duration::from_millis(400)).await;
        let v = t.drain();
        assert_eq!(v.len(), 0);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_task_completion_sans_polling() {
        let t = TaskManager::new();
        let (s, r) = async_channel::bounded(1);
        t.spawn(async move { s.send(()).await.unwrap() });
        r.recv().await.unwrap();
    }
}
