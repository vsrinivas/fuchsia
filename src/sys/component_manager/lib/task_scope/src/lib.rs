// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async::Task,
    futures::{lock::Mutex, Future, FutureExt},
    rand::{thread_rng, Rng},
    std::{collections::HashMap, sync::Arc},
};

/// Hosts tasks for a single component instance. If the scope is dropped, all tasks
/// in this scope are dropped as well. When the task execution completes, it is cleaned
/// up from this scope automatically.
#[derive(Debug, Clone)]
pub struct TaskScope {
    tasks: Arc<Mutex<HashMap<u128, Task<()>>>>,
}

impl TaskScope {
    pub fn new() -> Self {
        Self { tasks: Arc::new(Mutex::new(HashMap::new())) }
    }

    /// Adds a task to this scope and executes it
    pub async fn add_task<F: 'static + Future<Output = ()> + Send>(&self, future: F) {
        let id = thread_rng().gen::<u128>();
        let weak_tasks = Arc::downgrade(&self.tasks);
        let future = future.then(move |_| async move {
            // This may return None if the task list gets dropped, but the future associated
            // with the task has not stopped on the executor.
            if let Some(tasks) = weak_tasks.upgrade() {
                let _ = tasks.lock().await.remove(&id);
            }
        });
        // Acquire this lock before we spawn the future. This is to prevent a situation where
        // the future completes before we've inserted the task into the list.
        let mut locked_tasks = self.tasks.lock().await;
        let task = Task::spawn(future);
        locked_tasks.insert(id, task);
    }

    // Shuts down this task scope, preventing more tasks from being added.
    // Blocks until all tasks are complete.
    pub async fn shutdown(self) {
        let tasks: Vec<(u128, Task<()>)> = self.tasks.lock().await.drain().collect();
        drop(self.tasks);
        for (_, task) in tasks {
            task.await;
        }
    }
}
