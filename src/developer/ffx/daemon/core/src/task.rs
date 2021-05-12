// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    futures::future::FutureExt, futures::future::LocalBoxFuture, futures::future::Shared,
    std::cell::RefCell, std::collections::HashMap, std::fmt::Debug, std::hash::Hash, std::rc::Rc,
};

/// An abstraction for a one-task-at-a-time task manager.
///
/// Each task is explicitly spawned via an enum, for which there can only be
/// one running at a time. An arbitrary number of listeners can await these
/// Futures.
pub struct SingleFlight<T, R> {
    tasks: Rc<RefCell<HashMap<T, Shared<LocalBoxFuture<'static, R>>>>>,
    spawner: Box<dyn 'static + Fn(T) -> LocalBoxFuture<'static, R>>,
}

#[derive(Debug, PartialEq, Eq, Clone)]
pub enum TaskSnapshot {
    Running,
    NotRunning,
}

impl<T: Hash + Eq + PartialEq + Clone + 'static, R: Clone + 'static> SingleFlight<T, R> {
    /// Constructs a single flight manager. Accepts a function that turns an
    /// input value into a task of some kind. The return value of the task
    /// must be clonable as its output (see `spawn`) can be waited on by an
    /// arbitrary number of waiters.
    pub fn new(func: impl Fn(T) -> LocalBoxFuture<'static, R> + 'static) -> Self {
        let spawner = Box::new(func);
        Self { tasks: Rc::new(RefCell::new(HashMap::new())), spawner }
    }

    fn make_cleanup_task(&self, t: T) -> Shared<LocalBoxFuture<'static, R>> {
        // Converts the future into something that will
        // automagically clean itself up when complete.
        let task_fut = (self.spawner)(t.clone());
        let tasks = Rc::downgrade(&self.tasks);
        let task = async move {
            let res = task_fut.await;
            tasks.upgrade().map(|tasks| tasks.borrow_mut().remove(&t));
            res
        };
        task.boxed_local().shared()
    }

    /// Spawns a task at most once.
    ///
    /// After this function is awaited and completed, returns a future that must
    /// be polled on. If you do not wish to poll the function or care about
    /// the result of the calculation, use `spawn_detached()` instead.
    ///
    /// Your spawner function returns a future that needs to be
    /// polled in order to progress; it is essential that at least one invoker
    /// of this method poll the future, or else it will remain frozen and never
    /// complete.
    ///
    /// One task can run at a time for each unique `T`.
    ///
    /// When a task for a given `T` completes, it will atomically remove itself
    /// from the pool.
    #[allow(unused)] // Unused yet (will be used w/ paving, etc).
    pub async fn spawn(&self, t: T) -> Shared<LocalBoxFuture<'static, R>> {
        // This cannot be done using `or_insert` as the code inside that
        // function must not be executed if there is not an entry. Furthermore,
        // `or_insert_with` is not used as we need to exit prematurely in the
        // event that no task can be created. That is why this is a manual
        // `match` rather than purely functional.
        if let Some(task) = self.tasks.borrow().get(&t) {
            return task.clone();
        }

        let task = self.make_cleanup_task(t.clone());
        self.tasks.borrow_mut().insert(t, task.clone());
        task
    }

    /// Spawns an auto-cleaning task that runs at most once.
    ///
    /// Different from `spawn()` in that no future is returned. This polls
    /// itself and then cleans itself up from the tracked running tasks after
    /// completion.
    pub fn spawn_detached(&self, t: T) {
        if !self.tasks.borrow().contains_key(&t) {
            let task = self.make_cleanup_task(t.clone());
            self.tasks.borrow_mut().insert(t, task.clone());
            fuchsia_async::Task::local(task).detach();
        }
    }

    /// A basic task status check function.
    ///
    /// Returns a TaskSnapshot struct containing information about the task's
    /// state.
    ///
    /// This is a VERY racy function, and shouldn't be used for much
    /// beyond debugging/status strings on long running functions.
    ///
    /// For emphasis: do NOT use this function for control flow of any kind
    /// under any circumstances.
    pub fn task_snapshot(&self, t: T) -> TaskSnapshot {
        if self.tasks.borrow().contains_key(&t) {
            TaskSnapshot::Running
        } else {
            TaskSnapshot::NotRunning
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async::Task;
    use futures::lock::Mutex;
    use lazy_static::lazy_static;

    lazy_static! {
        // This is a bit of a hack, but makes for easier to read tests (instead
        // of making some arbitrary structs).
        static ref FIRE_ONCE: Mutex<bool> = Mutex::new(true);
    }

    async fn fire_once_global() -> bool {
        let mut f = FIRE_ONCE.lock().await;
        let res = *f;
        *f = false;
        res
    }

    #[derive(Hash, Eq, PartialEq, Clone)]
    enum TestTaskType {
        TaskThatGloballyRunsOnce,
        TaskThatExitsImmediately,
        TaskThatPollsItself,
    }

    fn setup() -> SingleFlight<TestTaskType, bool> {
        SingleFlight::new(move |t| match t {
            TestTaskType::TaskThatGloballyRunsOnce => fire_once_global().boxed_local(),
            TestTaskType::TaskThatExitsImmediately => futures::future::ready(true).boxed_local(),
            TestTaskType::TaskThatPollsItself => Task::local(async move { true }).boxed_local(),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_multiple_tasks_exit() {
        let mgr = setup();

        // The number of attempted spawns here is arbitrary.
        let (a, b, c, d) = futures::join!(
            mgr.spawn(TestTaskType::TaskThatExitsImmediately),
            mgr.spawn(TestTaskType::TaskThatGloballyRunsOnce),
            mgr.spawn(TestTaskType::TaskThatGloballyRunsOnce),
            mgr.spawn(TestTaskType::TaskThatGloballyRunsOnce),
        );

        assert_eq!(
            mgr.task_snapshot(TestTaskType::TaskThatExitsImmediately),
            TaskSnapshot::Running
        );
        assert_eq!(
            mgr.task_snapshot(TestTaskType::TaskThatGloballyRunsOnce),
            TaskSnapshot::Running
        );

        let (a, b, c, d) = futures::join!(a, b, c, d);
        assert!(a);
        assert!(b);
        assert!(c);
        assert!(d);

        assert_eq!(
            mgr.task_snapshot(TestTaskType::TaskThatExitsImmediately),
            TaskSnapshot::NotRunning
        );
        assert_eq!(
            mgr.task_snapshot(TestTaskType::TaskThatGloballyRunsOnce),
            TaskSnapshot::NotRunning
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_task_that_polls_itself() {
        let mgr = setup();
        assert!(mgr.spawn(TestTaskType::TaskThatPollsItself).await.await);
        assert_eq!(mgr.task_snapshot(TestTaskType::TaskThatPollsItself), TaskSnapshot::NotRunning);
    }
}
