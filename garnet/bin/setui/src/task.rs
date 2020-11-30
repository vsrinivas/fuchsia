// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Asynchronous task management and tracking
//!
//! # Summary
//!
//! The Task module helps manage and track asynchronous tasks by categorizing
//! tasks and processing them through a central authority.
//!
//! [`Manager`] helps track asynchronous tasks in the setting service. Created
//! by [`Builder`], [`Manager`] is informed of every asynchronous task spawned.
//! [`Manager`] is generically typed based on [`Category`] to allow flexibilty
//! in use cases.
//!
//! Tasks are created through the [`Manager`]'s counterpart [`Client`] by
//! specifying the task as a future and a [`Category`] that can identify the
//! task's purpose. In the current implementation, the [`Client`] communicates
//! the lifecycle events of the task to the [`Manager`].
//!
//! # Example
//!
//! ```no_run
//! # use crate::task::Builder;
//!
//! /// Task categories for specifying the task purpose.
//! #[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
//! pub enum Category {
//!   Foo(usize),
//! }
//!
//! pub fn execute_task() {
//!     // Create task manager. Normally this would be created at the top level,
//!     // and the cloneable client would be passed down to child components.
//!     let (processing_future, client) = Builder::<Category>::new().build();
//!
//!     // Initiate task manager thread (we do not manager the future in the
//!     // example so it is detached).
//!     processing_future.detach();
//!
//!     // Use client to run task.
//!     client.spawn(&Category::Foo(1), async move {
//!         println!("Hello!");
//!     });
//! }
//! ```
//! [`Client`]: struct.Client.html
//! [`Manager`]: struct.Manager.html
//! [`Builder`]: struct.Builder.html
//! [`Category`]: trait.Category.html

use chrono::{DateTime, Duration, Utc};
use core::fmt::{Debug, Formatter};
use fuchsia_async as fasync;
use futures::lock::Mutex;
use futures::prelude::*;
use futures::StreamExt;
use itertools::{sorted, Itertools};
use std::collections::HashMap;
use std::hash::Hash;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

/// The `Category` trait defines the data that can be used as a key for the
/// types of tasks managed by [`Manager`].
///
/// [`Manager`]: struct.Manager.html
pub trait Category: Clone + Debug + Eq + Hash + Send + Sync {}
impl<T: Clone + Debug + Eq + Hash + Send + Sync> Category for T {}

/// A unique identifier within the task space for a spawned task instance.
#[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
pub struct Id {
    key: usize,
}

/// a generator responsible for producing unique [`Id`].
/// [`Id`]: struct.Id.html
struct IdGenerator {
    next_id: AtomicUsize,
}

impl IdGenerator {
    /// Returns a handle to a new `IdGenerator`.
    pub fn create() -> Arc<Self> {
        Arc::new(Self { next_id: AtomicUsize::new(0) })
    }

    /// generates a unique identifier.
    pub fn generate(&self) -> Id {
        Id { key: self.next_id.fetch_add(1, Ordering::SeqCst) }
    }
}

/// The timestamp tracking lifetime of a task.
pub type Timestamp = DateTime<Utc>;

/// The handle to a given [`Sink`] outlet which can receive task summary data.
///
/// [`Sink`]: trait.Sink.html
type SinkHandle<C> = Arc<Mutex<dyn Sink<C> + Send + Sync>>;

/// Implementations of `Sink` are handed to [`Manager`] at creation.
///
/// [`Manager`]: struct.Manager.html
pub trait Sink<C: Category + 'static> {
    /// Invoked when new [`Summary`] information is available.
    ///
    /// [`Summary`]: struct.Summary.html
    fn process(&mut self, summary: &Summary<C>);
}

/// Task actions specified to [`Manager`].
///
/// [`Manager`]: struct.Manager.html
enum Action<C: Category + 'static> {
    /// Create indicates a task is being spawned for the given [`Category`].
    /// [`Category`]: enum.Category.html
    Create(C),

    /// Complete indicates the task associated with this action has exited.
    Complete,
}

/// `Client` is passed to other parts of the codebase to spawn new tasks. This
/// interface takes care of communicating the task actions to the underlying
/// [Manager].
///
/// [Manager]: struct.Manager.html
pub struct Client<C: Category + 'static> {
    /// A sender to communicate changes back to the [`Manager`].
    ///
    /// [`Manager`]: struct.Manager.html
    action_tx: futures::channel::mpsc::UnboundedSender<(Id, Action<C>, Timestamp)>,
    /// A generator for retrieving an identifier to associate with a task.
    id_generator: Arc<IdGenerator>,
}

#[allow(dead_code)]
impl<C: Category + 'static> Client<C> {
    /// Spawns a new task, tracking its lifetime with the provided [`Category`].
    ///
    /// [`Category`]: enum.Category.html
    pub fn spawn<T: Send>(&self, category: &C, future: impl Future<Output = T> + Send + 'static) {
        let task_id = self.id_generator.generate();

        // We report the creation before spawning in case spawning fails.
        self.action_tx.unbounded_send((task_id, Action::Create(category.clone()), Utc::now())).ok();

        // Pass a clone of the action sender to the spawned task in order to
        // signal when the task completes.
        let action_tx = self.action_tx.clone();
        fasync::Task::spawn(async move {
            future.await;

            // Report exit.
            action_tx.unbounded_send((task_id, Action::Complete, Utc::now())).ok();
        })
        .detach();
    }
}

/// `Summary` provides an overview of task activity through the [`Manager`].
/// This information is passed to [`Sink`] instances.
///
/// [`Manager`]: struct.Manager.html
/// [`Sink`]: trait.Sink.html
#[derive(Clone)]
pub struct Summary<C: Category + 'static> {
    /// Current tasks that are underway. Mapping helps update the task category
    /// when a task completes.
    active_tasks: HashMap<Id, C>,

    /// Mapping of seen task categories to category statistics.
    statistics: HashMap<C, Statistics<C>>,
}

#[allow(dead_code)]
impl<C: Category + 'static> Summary<C> {
    fn new() -> Self {
        Self { active_tasks: HashMap::new(), statistics: HashMap::new() }
    }

    /// Invoked by [`Manager`] when a new task [`Action`] occurs.
    ///
    /// [`Manager`]: struct.Manager.html
    /// [`Action`]: enum.Action.html
    fn ingest(&mut self, id: Id, action: Action<C>, timestamp: Timestamp) {
        match action {
            Action::Create(category) => {
                self.active_tasks.insert(id, category.clone());

                // Ensure stats exist for the particular category.
                if !self.statistics.contains_key(&category) {
                    self.statistics
                        .insert(category.clone(), Statistics::<C>::new(category.clone()));
                }

                // Inform stats of task start.
                self.statistics
                    .get_mut(&category)
                    .expect("category should be present")
                    .start(id, timestamp);
            }
            Action::Complete => {
                // Inform completion.
                self.statistics
                    .get_mut(&self.active_tasks.remove(&id).expect("id should be present"))
                    .expect("stats must be present")
                    .complete(id, timestamp);
            }
        }
    }

    /// Returns the longest running active task and its start time.
    pub fn get_longest_active_task(&self) -> Option<(C, Timestamp)> {
        let mut return_val = None;
        for statistics in self.statistics.values() {
            for timestamp in statistics.active_tasks.values() {
                return_val = Some(return_val.map_or_else(
                    || (statistics.category.clone(), timestamp.clone()),
                    |x: (C, Timestamp)| {
                        if timestamp < &x.1 {
                            (statistics.category.clone(), timestamp.clone())
                        } else {
                            x
                        }
                    },
                ));
            }
        }

        return_val
    }

    /// Returns the completed task count.
    pub fn get_completed_task_count(&self) -> i64 {
        self.statistics.values().into_iter().map(|stat| stat.lifetime_count).sum()
    }

    /// Returns the active task count.
    pub fn get_active_task_count(&self) -> usize {
        self.active_tasks.len()
    }

    /// Returns the categories with currently running tasks.
    pub fn get_active_categories(&self) -> Vec<C> {
        self.active_tasks.values().into_iter().unique().cloned().collect()
    }

    /// Returns the seen categories.
    pub fn get_seen_categories(&self) -> Vec<C> {
        self.statistics.keys().cloned().collect()
    }

    /// Get the [`Statistics`] for a given [`Category`].
    ///
    /// [`Category`]: enum.Category.html
    /// [`Statistics`]: struct.Statistics.html
    pub fn get_statistics(&self, category: &C) -> Option<Statistics<C>> {
        self.statistics.get(&category).cloned()
    }

    /// Returns the [`Category`] and duration of the longest completed task.
    ///
    /// [`Category`]: enum.Category.html
    pub fn longest_completed_task(&self) -> Option<(C, Duration)> {
        let mut return_val = None;
        while let Some(statistics) = self.statistics.values().next() {
            if let Some(duration) = statistics.longest_duration {
                return_val = Some(return_val.map_or_else(
                    || (statistics.category.clone(), duration),
                    |x: (C, Duration)| {
                        if duration > x.1 {
                            (statistics.category.clone(), duration)
                        } else {
                            x
                        }
                    },
                ));
            }
        }

        return_val
    }
}

#[derive(Clone)]
pub struct Statistics<C: Category + 'static> {
    /// The category represented by the given Statistics.
    pub category: C,
    /// The number of completed tasks.
    pub lifetime_count: i64,
    /// The average amount of time spent completing tasks.
    pub average_duration: Option<Duration>,
    /// The longest amount of time spent completing a task.
    pub longest_duration: Option<Duration>,
    /// The id and start time for currently active tasks in this category.
    pub active_tasks: HashMap<Id, Timestamp>,
}

#[allow(dead_code)]
impl<C: Category + 'static> Statistics<C> {
    /// Creates a `Statistics` instance with default values.
    pub fn new(category: C) -> Self {
        Self {
            category,
            lifetime_count: 0,
            average_duration: None,
            longest_duration: None,
            active_tasks: HashMap::new(),
        }
    }

    /// Returns the start time of the oldest actively running task.
    pub fn get_oldest_active_start_time(&self) -> Option<Timestamp> {
        sorted(self.active_tasks.values().into_iter()).next().cloned()
    }

    /// Returns the number of tasks actively running.
    pub fn get_active_task_count(&self) -> usize {
        self.active_tasks.len()
    }

    /// Ingests data around the start of a task.
    pub fn start(&mut self, id: Id, start_time: Timestamp) {
        self.active_tasks.insert(id, start_time);
    }

    /// Ingests data surrounding the end of a task.
    pub fn complete(&mut self, id: Id, end_time: Timestamp) {
        let start_time = self
            .active_tasks
            .remove(&id)
            .expect(&format!("timestamp for {:?} should be present", id));
        let duration = end_time.signed_duration_since(start_time);

        self.lifetime_count += 1;

        // Adjust the average duration of completed tasks.
        self.average_duration = Some(self.average_duration.map_or(duration, |x| {
            Duration::milliseconds(
                (x.num_milliseconds() * (self.lifetime_count - 1) + duration.num_milliseconds())
                    / self.lifetime_count,
            )
        }));

        // Update the longest duration of completed tasks if necessary.
        self.longest_duration =
            Some(self.longest_duration.map_or(
                duration,
                |x| {
                    if duration > x {
                        duration
                    } else {
                        x
                    }
                },
            ));
    }
}

impl<C: Category + 'static> Debug for Statistics<C> {
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        write!(
            f,
            "category:{:?} [completed] count:{:?} longest:{:?} average:{:?} [active] count:{:?}",
            self.category,
            self.lifetime_count,
            self.longest_duration,
            self.average_duration,
            self.active_tasks.len()
        )
    }
}

/// `Builder` provides a way to construct a [`Manager`] by specifying an
/// unbound set of [`SinkHandle`].
///
/// [`Manager`]: struct.Manager.html
/// [`SinkHandle`]: type.SinkHandle.html
pub struct Builder<C: Category + 'static> {
    sinks: Vec<SinkHandle<C>>,
}

#[allow(dead_code)]
impl<C: Category + 'static> Builder<C> {
    pub fn new() -> Self {
        Self { sinks: Vec::new() }
    }

    pub fn add_sink(mut self, handle: SinkHandle<C>) -> Self {
        self.sinks.push(handle);

        self
    }

    pub fn build(self) -> (fasync::Task<()>, Client<C>) {
        Manager::create(self.sinks)
    }
}

/// `Manager` receives signals from its counterpart [`Client`] to track active
/// tasks and statistics around them.
///
/// [`Client`]: struct.Client.html
pub struct Manager<C: Category + 'static> {
    summary: Summary<C>,
    sinks: Vec<SinkHandle<C>>,
}

#[allow(dead_code)]
impl<C: Category + 'static> Manager<C> {
    /// Instantiates a new `Manager` instance and returns a tuple containg
    /// the processing Task and [`Client`] to interact.
    ///
    /// [`Client`]: struct.Client.html
    pub fn create(sinks: Vec<SinkHandle<C>>) -> (fasync::Task<()>, Client<C>) {
        // Create an unbounded channel for clients to communicate with the task
        // manager.
        let (action_tx, mut action_rx) =
            futures::channel::mpsc::unbounded::<(Id, Action<C>, Timestamp)>();
        let mut manager = Self { sinks, summary: Summary::<C>::new() };

        // This should be one of the only untracked async tasks in the codebase.
        let processing_future = fasync::Task::spawn(async move {
            while let Some((id, action, timestamp)) = action_rx.next().await {
                manager.handle(id, action, timestamp).await;
            }
        });

        (processing_future, Client { action_tx, id_generator: IdGenerator::create() })
    }

    async fn handle(&mut self, id: Id, action: Action<C>, timestamp: Timestamp) {
        self.summary.ingest(id, action, timestamp);

        for sink in &self.sinks {
            sink.lock().await.process(&self.summary);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
    pub enum TestCategory {
        Foo(usize),
    }

    type TestSummary = Summary<TestCategory>;

    struct TestSink {
        sender: futures::channel::mpsc::UnboundedSender<TestSummary>,
    }

    impl TestSink {
        fn create() -> (Arc<Mutex<Self>>, futures::channel::mpsc::UnboundedReceiver<TestSummary>) {
            let (summary_tx, summary_rx) = futures::channel::mpsc::unbounded::<TestSummary>();
            (Arc::new(Mutex::new(Self { sender: summary_tx })), summary_rx)
        }
    }

    impl Sink<TestCategory> for TestSink {
        fn process(&mut self, summary: &TestSummary) {
            self.sender.unbounded_send(summary.clone()).ok();
        }
    }

    /// Verifies the adjustments to `Summary` and `Statistics` based on the
    /// actions around a single task.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_single_task() {
        let (sink, mut receiver) = TestSink::create();
        let (process_future, client) = Builder::<TestCategory>::new().add_sink(sink).build();
        process_future.detach();

        let category = TestCategory::Foo(1);

        // Capture timestamp before spawning to verify any action is recorded
        // after this point.
        let before_timestamp = Utc::now();
        client.spawn(&category, async move {});

        // After spawning a task, the manager should send an update indicating
        // a single active tasks with no completed tasks.
        {
            let summary = receiver.next().await.expect("should get summary");
            verify_summary(&summary, 1, 0);

            let active_categories = summary.get_active_categories();
            assert!(active_categories.contains(&category) && active_categories.len() == 1);
            let stats = summary.get_statistics(&category).expect("should be found");
            assert!(
                stats.get_oldest_active_start_time().expect("should be found") >= before_timestamp
            );
        }

        // After the task exits, the completion count should increase and there
        // should not be any active tasks left.
        {
            let summary = receiver.next().await.expect("should get summary");
            assert!(summary.get_longest_active_task().is_none());
            verify_summary(&summary, 0, 1);
            assert!(summary.get_active_categories().is_empty());
            let stats = summary
                .get_statistics(&category)
                .expect("statistics for the category should be present");
            assert_eq!(stats.lifetime_count, 1);
            assert_eq!(stats.average_duration, stats.longest_duration);
        }
    }

    /// Verifies `Summary` and associated `Statistics` properly reflect changes
    /// in tasks.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_multiple_tasks() {
        let (sink, mut receiver) = TestSink::create();
        let (process_future, client) = Builder::<TestCategory>::new().add_sink(sink).build();
        process_future.detach();

        let category_1 = TestCategory::Foo(1);

        // Spawn first task.
        let (future_1, exit_tx_1) = create_exitable_future();
        client.spawn(&category_1, future_1);

        let start_time_1;

        {
            let summary = receiver.next().await.expect("should get summary");
            // Ensure only one task is active and none has completed.
            verify_summary(&summary, 1, 0);

            // There should be a single active category matching the new task.
            let active_categories = summary.get_active_categories();
            assert!(active_categories.contains(&category_1) && active_categories.len() == 1);
            let stats = summary.get_statistics(&category_1).expect("should be found");

            // The oldest start time should be the task just created.
            start_time_1 = stats.get_oldest_active_start_time();
        }

        let category_2 = TestCategory::Foo(2);

        // Spawn second task.
        let (future_2, exit_tx_2) = create_exitable_future();
        client.spawn(&category_2, future_2);

        let start_time_2;

        {
            let summary = receiver.next().await.expect("should get summary");

            // The active count should have grown.
            verify_summary(&summary, 2, 0);

            // Active categories should have grown to include the second task's
            // category.
            let active_categories = summary.get_active_categories();
            assert!(active_categories.contains(&category_2) && active_categories.len() == 2);
            let stats = summary.get_statistics(&category_2).expect("should be found");

            start_time_2 = stats.get_oldest_active_start_time();

            // Ensure the first task's start time is still the oldest.
            assert_eq!(
                summary.get_longest_active_task().expect("should have longest active task").1,
                start_time_1.expect("first start time should be present")
            );
        }

        // Signal first task to exit.
        exit_tx_1.send(()).ok();

        {
            let summary = receiver.next().await.expect("should get summary");
            // Only one task should be active with another completed.
            verify_summary(&summary, 1, 1);

            let active_categories = summary.get_active_categories();

            // Active categories should shrink to just the second task's
            // category.
            assert!(active_categories.contains(&category_2) && active_categories.len() == 1);

            let stats_1 = summary.get_statistics(&category_1).expect("should be found");

            // The first task's category should no longer have active tasks.
            assert!(stats_1.get_oldest_active_start_time().is_none());

            // The second task should be the longest active task.
            assert_eq!(
                summary.get_longest_active_task().expect("should have longest active task").1,
                start_time_2.expect("first start time should be present")
            );
        }

        // Signal second task to exit.
        exit_tx_2.send(()).ok();

        {
            let summary = receiver.next().await.expect("should get summary");

            // There should not be any more active tasks.
            verify_summary(&summary, 0, 2);

            let active_categories = summary.get_active_categories();
            assert!(active_categories.is_empty());

            assert!(summary.get_longest_active_task().is_none());

            let stats_2 = summary.get_statistics(&category_2).expect("should be found");

            // The second task's category should not report any active tasks.
            assert!(stats_2.get_oldest_active_start_time().is_none());
        }
    }

    fn create_exitable_future() -> (impl Future<Output = ()>, futures::channel::oneshot::Sender<()>)
    {
        let (tx, rx) = futures::channel::oneshot::channel::<()>();
        (
            async move {
                let _ = rx.await;
            },
            tx,
        )
    }

    fn verify_summary<C: Category + 'static>(
        summary: &Summary<C>,
        active_count: usize,
        completed_count: i64,
    ) {
        assert_eq!(summary.get_active_task_count(), active_count);
        assert_eq!(summary.get_completed_task_count(), completed_count);
    }
}
