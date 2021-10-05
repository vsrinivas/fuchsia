// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics::{
            component_stats::ComponentStats,
            constants::*,
            measurement::Measurement,
            runtime_stats_source::{
                DiagnosticsReceiverProvider, RuntimeStatsContainer, RuntimeStatsSource,
            },
            task_info::{create_cpu_histogram, TaskInfo},
        },
        model::error::ModelError,
        model::hooks::{Event, EventPayload, EventType, HasEventType, Hook, HooksRegistration},
    },
    async_trait::async_trait,
    fidl_fuchsia_diagnostics_types::Task as DiagnosticsTask,
    fuchsia_async as fasync,
    fuchsia_inspect::{self as inspect, HistogramProperty},
    fuchsia_inspect_contrib::nodes::BoundedListNode,
    fuchsia_zircon::{self as zx, HandleBased},
    fuchsia_zircon_sys as zx_sys,
    futures::{channel::oneshot, lock::Mutex, FutureExt},
    injectable_time::MonotonicTime,
    log::warn,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ExtendedMoniker},
    std::{
        collections::BTreeMap,
        sync::{Arc, Weak},
    },
};

macro_rules! maybe_return {
    ($e:expr) => {
        match $e {
            None => return,
            Some(v) => v,
        }
    };
}

const MAX_INSPECT_SIZE : usize = 2 * 1024 * 1024 /* 2MB */;

/// Provides stats for all components running in the system.
pub struct ComponentTreeStats<T: RuntimeStatsSource> {
    /// Map from a moniker of a component running in the system to its stats.
    tree: Mutex<BTreeMap<ExtendedMoniker, Arc<Mutex<ComponentStats<T>>>>>,

    /// Stores all the tasks we know about. This provides direct access for updating a task's
    /// children.
    tasks: Mutex<BTreeMap<zx_sys::zx_koid_t, Weak<Mutex<TaskInfo<T, MonotonicTime>>>>>,

    /// The root of the tree stats.
    node: inspect::Node,

    /// The node under which CPU usage histograms will be stored.
    histograms_node: inspect::Node,

    /// A histogram storing stats about the time it took to process the CPU stats measurements.
    processing_times: inspect::IntExponentialHistogramProperty,

    /// The task that takes CPU samples every minute.
    sampler_task: Mutex<Option<fasync::Task<()>>>,

    /// Aggregated CPU stats.
    totals: Mutex<AggregatedStats>,
}

impl<T: 'static + RuntimeStatsSource + Send + Sync> ComponentTreeStats<T> {
    pub async fn new(node: inspect::Node) -> Arc<Self> {
        let processing_times = node.create_int_exponential_histogram(
            "processing_times_ns",
            inspect::ExponentialHistogramParams {
                floor: 1000,
                initial_step: 1000,
                step_multiplier: 2,
                buckets: 16,
            },
        );

        let histograms_node = node.create_child("histograms");
        let totals = AggregatedStats::new(node.create_child("@total"));
        let this = Arc::new(Self {
            tree: Mutex::new(BTreeMap::new()),
            tasks: Mutex::new(BTreeMap::new()),
            node,
            histograms_node,
            processing_times,
            sampler_task: Mutex::new(None),
            totals: Mutex::new(totals),
        });

        let weak_self = Arc::downgrade(&this);

        let weak_self_for_fut = weak_self.clone();
        this.node.record_lazy_child("measurements", move || {
            let weak_self_clone = weak_self_for_fut.clone();
            async move {
                if let Some(this) = weak_self_clone.upgrade() {
                    Ok(this.write_measurements_to_inspect().await)
                } else {
                    Ok(inspect::Inspector::new())
                }
            }
            .boxed()
        });
        let weak_self_clone = weak_self.clone();
        this.node.record_lazy_child("recent_usage", move || {
            let weak_self_clone = weak_self.clone();
            async move {
                if let Some(this) = weak_self_clone.upgrade() {
                    Ok(this.write_recent_usage_to_inspect().await)
                } else {
                    Ok(inspect::Inspector::new())
                }
            }
            .boxed()
        });

        this.measure().await;
        *(this.sampler_task.lock().await) = Some(Self::spawn_measuring_task(weak_self_clone));

        this
    }

    /// Initializes a new component stats with the given task.
    async fn track_ready(&self, moniker: ExtendedMoniker, task: T) {
        let histogram = create_cpu_histogram(&self.histograms_node, &moniker);
        if let Ok(task_info) = TaskInfo::try_from(task, Some(histogram)) {
            let koid = task_info.koid();
            let stats = ComponentStats::ready(task_info);
            self.tasks.lock().await.insert(koid, Arc::downgrade(&stats.tasks()[0]));
            self.tree.lock().await.insert(moniker.clone(), Arc::new(Mutex::new(stats)));
        }
    }

    async fn write_measurements_to_inspect(self: &Arc<Self>) -> inspect::Inspector {
        let inspector = inspect::Inspector::new_with_size(MAX_INSPECT_SIZE);
        let components = inspector.root().create_child("components");
        let task_count = self.write_measurements(&components).await;
        inspector.root().record_uint("task_count", task_count);
        inspector.root().record(components);

        let stats_node = inspect::stats::Node::snapshot(&inspector, &inspector.root());
        inspector.root().record(stats_node.take());

        inspector
    }

    async fn write_recent_usage_to_inspect(self: &Arc<Self>) -> inspect::Inspector {
        let inspector = inspect::Inspector::new();
        self.totals.lock().await.write_recents_to(inspector.root());
        inspector
    }

    async fn write_measurements(&self, node: &inspect::Node) -> u64 {
        let mut task_count = 0;
        for (moniker, stats) in self.tree.lock().await.iter() {
            let stats_guard = stats.lock().await;
            if stats_guard.is_measuring() {
                // TODO(fxbug.dev/73169): unify diagnostics and component manager monikers.
                let key = match moniker {
                    ExtendedMoniker::ComponentManager => moniker.to_string(),
                    ExtendedMoniker::ComponentInstance(m) => {
                        if *m == AbsoluteMoniker::root() {
                            "<root>".to_string()
                        } else {
                            m.to_string_without_instances().replacen("/", "", 1)
                        }
                    }
                };
                let child = node.create_child(key);
                task_count += stats_guard.record_to_node(&child).await;
                node.record(child);
            }
        }
        task_count
    }

    /// Takes a measurement of all tracked tasks and updated the totals. If any task is not alive
    /// anymore it deletes it. If any component is not alive any more and no more historical
    /// measurements are available for it, deletes it too.
    pub async fn measure(self: &Arc<Self>) {
        let start = zx::Time::get_monotonic();

        // Copy the stats and release the lock.
        let stats =
            self.tree.lock().await.iter().map(|(k, v)| (k.clone(), v.clone())).collect::<Vec<_>>();
        let mut aggregated = Measurement::empty(start);
        let mut stats_to_remove = vec![];
        let mut koids_to_remove = vec![];
        for (moniker, stat) in stats.into_iter() {
            let mut stat_guard = stat.lock().await;
            aggregated += &stat_guard.measure().await;
            koids_to_remove.append(&mut stat_guard.clean_stale().await);
            if !stat_guard.is_alive().await {
                stats_to_remove.push(moniker);
            }
        }

        // Lock the tree so that we ensure no modifications are made while we are deleting
        let mut stats = self.tree.lock().await;
        for moniker in stats_to_remove {
            // Ensure that they are still not alive (if a component restarted it might be alive
            // again).
            if let Some(stat) = stats.get(&moniker) {
                if !stat.lock().await.is_alive().await {
                    stats.remove(&moniker);
                }
            }
        }

        let mut tasks = self.tasks.lock().await;
        for koid in koids_to_remove {
            tasks.remove(&koid);
        }

        self.totals.lock().await.update(aggregated);
        self.processing_times.insert((zx::Time::get_monotonic() - start).into_nanos());
    }

    fn spawn_measuring_task(weak_self: Weak<Self>) -> fasync::Task<()> {
        fasync::Task::spawn(async move {
            loop {
                fasync::Timer::new(CPU_SAMPLE_PERIOD).await;
                match weak_self.upgrade() {
                    None => break,
                    Some(this) => {
                        this.measure().await;
                    }
                }
            }
        })
    }

    async fn on_component_started<P, C>(self: &Arc<Self>, moniker: ExtendedMoniker, runtime: &P)
    where
        P: DiagnosticsReceiverProvider<C, T>,
        C: RuntimeStatsContainer<T> + Send + Sync + 'static,
    {
        let mut tree_guard = self.tree.lock().await;
        if tree_guard.contains_key(&moniker) {
            return;
        }

        if let Some(receiver) = runtime.get_receiver().await {
            let task = fasync::Task::spawn(Self::diagnostics_waiter_task(
                Arc::downgrade(&self),
                moniker.clone(),
                receiver,
            ));
            tree_guard.insert(moniker, Arc::new(Mutex::new(ComponentStats::pending(task))));
        }
    }

    async fn diagnostics_waiter_task<C>(
        weak_self: Weak<Self>,
        moniker: ExtendedMoniker,
        receiver: oneshot::Receiver<C>,
    ) where
        C: RuntimeStatsContainer<T> + Send + Sync + 'static,
    {
        let mut source = maybe_return!(receiver.await.ok());
        let this = maybe_return!(weak_self.upgrade());
        let mut tree_lock = this.tree.lock().await;
        let stats = maybe_return!(tree_lock.get_mut(&moniker));
        let histogram = create_cpu_histogram(&this.histograms_node, &moniker);
        let mut task_info = maybe_return!(source
            .take_component_task()
            .and_then(|task| TaskInfo::try_from(task, Some(histogram)).ok()));

        let histogram = create_cpu_histogram(&this.histograms_node, &moniker);
        let parent_koid = source
            .take_parent_task()
            .and_then(|task| TaskInfo::try_from(task, Some(histogram)).ok())
            .map(|task| task.koid());
        let koid = task_info.koid();
        let mut task_guard = this.tasks.lock().await;

        // At this point we haven't set the parent yet. We take an initial measurement of the
        // individual task.
        task_info.measure_if_no_parent().await;

        let task_info = match parent_koid {
            None => {
                // If there's no parent task measure this task directly, otherwise
                // we'll measure on the parent.
                Arc::new(Mutex::new(task_info))
            }
            Some(parent_koid) => {
                task_info.has_parent_task = true;
                let task_info = Arc::new(Mutex::new(task_info));
                if let Some(parent) = task_guard.get(&parent_koid).and_then(|p| p.upgrade()) {
                    let mut parent_guard = parent.lock().await;
                    parent_guard.add_child(Arc::downgrade(&task_info));
                }
                task_info
            }
        };
        task_guard.insert(koid, Arc::downgrade(&task_info));
        stats.lock().await.add_task(task_info).await;
    }
}

impl ComponentTreeStats<DiagnosticsTask> {
    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "ComponentTreeStats",
            vec![EventType::Started],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    /// Starts tracking component manager own stats.
    pub async fn track_component_manager_stats(&self) {
        match fuchsia_runtime::job_default().duplicate_handle(zx::Rights::SAME_RIGHTS) {
            Ok(job) => {
                self.track_ready(ExtendedMoniker::ComponentManager, DiagnosticsTask::Job(job))
                    .await;
            }
            Err(err) => warn!(
                "Failed to duplicate component manager job. Not tracking its own stats: {:?}",
                err
            ),
        }
    }
}

#[async_trait]
impl Hook for ComponentTreeStats<DiagnosticsTask> {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        let target_moniker: ExtendedMoniker = event
            .target_moniker
            .unwrap_instance_moniker_or(ModelError::UnexpectedComponentManagerMoniker)?
            .clone()
            .into();
        match event.event_type() {
            EventType::Started => {
                if let Some(EventPayload::Started { runtime, .. }) = event.result.as_ref().ok() {
                    self.on_component_started(target_moniker, runtime).await;
                }
            }
            _ => {}
        }
        Ok(())
    }
}

struct AggregatedStats {
    /// Holds historical aggregated CPU usage stats.
    node: BoundedListNode,

    /// Second most recent total recorded.
    previous_measurement: Option<Measurement>,

    /// Most recent total recorded.
    recent_measurement: Option<Measurement>,
}

impl AggregatedStats {
    fn new(node: inspect::Node) -> Self {
        let node = BoundedListNode::new(node, COMPONENT_CPU_MAX_SAMPLES);
        Self { node, previous_measurement: None, recent_measurement: None }
    }

    fn update(&mut self, measurement: Measurement) {
        // TODO(fxbug.dev/66596): use atomic updates here. We can't use inspect_log since it sets
        // automatically a timestamp for the entry.
        let child = self.node.create_entry();
        child.record_int("timestamp", measurement.timestamp().into_nanos());
        child.record_int("cpu_time", measurement.cpu_time().into_nanos());
        child.record_int("queue_time", measurement.queue_time().into_nanos());
        self.previous_measurement = self.recent_measurement.take();
        self.recent_measurement = Some(measurement);
    }

    fn write_recents_to(&self, node: &inspect::Node) {
        if let Some(measurement) = &self.previous_measurement {
            node.record_int("previous_cpu_time", measurement.cpu_time().into_nanos());
            node.record_int("previous_queue_time", measurement.queue_time().into_nanos());
            node.record_int("previous_timestamp", measurement.timestamp().into_nanos());
        }
        if let Some(measurement) = &self.recent_measurement {
            node.record_int("recent_cpu_time", measurement.cpu_time().into_nanos());
            node.record_int("recent_queue_time", measurement.queue_time().into_nanos());
            node.record_int("recent_timestamp", measurement.timestamp().into_nanos());
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            diagnostics::testing::{FakeDiagnosticsContainer, FakeRuntime, FakeTask},
            model::testing::routing_test_helpers::RoutingTest,
        },
        cm_rust_testing::ComponentDeclBuilder,
        diagnostics_hierarchy::DiagnosticsHierarchy,
        fuchsia_inspect::testing::{assert_data_tree, AnyProperty},
        fuchsia_zircon::{AsHandleRef, DurationNum},
        moniker::AbsoluteMoniker,
    };

    #[fuchsia::test]
    async fn components_are_deleted_when_all_tasks_are_gone() {
        let inspector = inspect::Inspector::new();
        let stats = ComponentTreeStats::new(inspector.root().create_child("cpu_stats")).await;
        let moniker: AbsoluteMoniker = vec!["a:0"].into();
        let moniker: ExtendedMoniker = moniker.into();
        stats.track_ready(moniker.clone(), FakeTask::default()).await;
        for _ in 0..=COMPONENT_CPU_MAX_SAMPLES {
            stats.measure().await;
        }
        assert_eq!(stats.tree.lock().await.len(), 1);
        assert_eq!(stats.tasks.lock().await.len(), 1);
        assert_eq!(
            stats.tree.lock().await.get(&moniker).unwrap().lock().await.total_measurements().await,
            COMPONENT_CPU_MAX_SAMPLES
        );

        // Invalidate the handle, to simulate that the component stopped.
        for task in
            stats.tree.lock().await.get(&moniker).unwrap().lock().await.tasks_mut().iter_mut()
        {
            task.lock().await.task_mut().invalid_handle = true;
        }

        for i in 0..COMPONENT_CPU_MAX_SAMPLES {
            stats.measure().await;
            assert_eq!(
                stats
                    .tree
                    .lock()
                    .await
                    .get(&moniker)
                    .unwrap()
                    .lock()
                    .await
                    .total_measurements()
                    .await,
                COMPONENT_CPU_MAX_SAMPLES - i,
            );
        }
        stats.measure().await;
        assert!(stats.tree.lock().await.get(&moniker).is_none());
        assert_eq!(stats.tree.lock().await.len(), 0);
        assert_eq!(stats.tasks.lock().await.len(), 0);
    }

    #[fuchsia::test]
    async fn total_holds_sum_of_stats() {
        let inspector = inspect::Inspector::new();
        let stats = ComponentTreeStats::new(inspector.root().create_child("cpu_stats")).await;
        stats
            .track_ready(
                ExtendedMoniker::ComponentInstance(vec!["a:0"].into()),
                FakeTask::new(
                    1,
                    vec![
                        zx::TaskRuntimeInfo {
                            cpu_time: 2,
                            queue_time: 4,
                            ..zx::TaskRuntimeInfo::default()
                        },
                        zx::TaskRuntimeInfo {
                            cpu_time: 6,
                            queue_time: 8,
                            ..zx::TaskRuntimeInfo::default()
                        },
                    ],
                ),
            )
            .await;
        stats
            .track_ready(
                ExtendedMoniker::ComponentInstance(vec!["b:0"].into()),
                FakeTask::new(
                    2,
                    vec![
                        zx::TaskRuntimeInfo {
                            cpu_time: 1,
                            queue_time: 3,
                            ..zx::TaskRuntimeInfo::default()
                        },
                        zx::TaskRuntimeInfo {
                            cpu_time: 5,
                            queue_time: 7,
                            ..zx::TaskRuntimeInfo::default()
                        },
                    ],
                ),
            )
            .await;

        stats.measure().await;
        let hierarchy = inspect::reader::read(&inspector).await.expect("read inspect hierarchy");
        let total_cpu_time = get_total_property(&hierarchy, 1, "cpu_time");
        let total_queue_time = get_total_property(&hierarchy, 1, "queue_time");
        assert_eq!(total_cpu_time, 2 + 1);
        assert_eq!(total_queue_time, 4 + 3);

        stats.measure().await;
        let hierarchy = inspect::reader::read(&inspector).await.expect("read inspect hierarchy");
        let total_cpu_time = get_total_property(&hierarchy, 2, "cpu_time");
        let total_queue_time = get_total_property(&hierarchy, 2, "queue_time");
        assert_eq!(total_cpu_time, 6 + 5);
        assert_eq!(total_queue_time, 8 + 7);
    }

    #[fuchsia::test]
    async fn recent_usage() {
        // Set up the test
        let inspector = inspect::Inspector::new();
        let stats = ComponentTreeStats::new(inspector.root().create_child("cpu_stats")).await;
        stats
            .track_ready(
                ExtendedMoniker::ComponentInstance(vec!["a:0"].into()),
                FakeTask::new(
                    1,
                    vec![
                        zx::TaskRuntimeInfo {
                            cpu_time: 2,
                            queue_time: 4,
                            ..zx::TaskRuntimeInfo::default()
                        },
                        zx::TaskRuntimeInfo {
                            cpu_time: 6,
                            queue_time: 8,
                            ..zx::TaskRuntimeInfo::default()
                        },
                    ],
                ),
            )
            .await;
        stats
            .track_ready(
                ExtendedMoniker::ComponentInstance(vec!["b:0"].into()),
                FakeTask::new(
                    2,
                    vec![
                        zx::TaskRuntimeInfo {
                            cpu_time: 1,
                            queue_time: 3,
                            ..zx::TaskRuntimeInfo::default()
                        },
                        zx::TaskRuntimeInfo {
                            cpu_time: 5,
                            queue_time: 7,
                            ..zx::TaskRuntimeInfo::default()
                        },
                    ],
                ),
            )
            .await;

        stats.measure().await;
        let hierarchy = inspect::reader::read(&inspector).await.expect("read inspect hierarchy");

        // Verify initially there's no second most recent measurement since we only
        // have the initial measurement written.
        assert_data_tree!(&hierarchy, root: contains {
            cpu_stats: contains {
                recent_usage: {
                    previous_cpu_time: 0i64,
                    previous_queue_time: 0i64,
                    previous_timestamp: AnyProperty,
                    recent_cpu_time: 2 + 1i64,
                    recent_queue_time: 4 + 3i64,
                    recent_timestamp: AnyProperty,
                }
            }
        });

        // Verify that the recent values are equal to the total values.
        let initial_timestamp = get_recent_property(&hierarchy, "recent_timestamp");
        assert_eq!(2 + 1, get_total_property(&hierarchy, 1, "cpu_time"));
        assert_eq!(4 + 3, get_total_property(&hierarchy, 1, "queue_time"));
        assert_eq!(initial_timestamp, get_total_property(&hierarchy, 1, "timestamp"));

        // Add one measurement
        stats.measure().await;
        let hierarchy = inspect::reader::read(&inspector).await.expect("read inspect hierarchy");

        // Verify that previous is now there and holds the previously recent values.
        assert_data_tree!(&hierarchy, root: contains {
            cpu_stats: contains {
                recent_usage: {
                    previous_cpu_time: 2 + 1i64,
                    previous_queue_time: 4 + 3i64,
                    previous_timestamp: initial_timestamp,
                    recent_cpu_time: 6 + 5i64,
                    recent_queue_time: 8 + 7i64,
                    recent_timestamp: AnyProperty,
                }
            }
        });

        // Verify that the recent timestamp is higher than the previous timestamp.
        let recent_timestamp = get_recent_property(&hierarchy, "recent_timestamp");
        assert!(recent_timestamp > initial_timestamp);
    }

    #[fuchsia::test]
    async fn component_stats_are_available_in_inspect() {
        let inspector = inspect::Inspector::new();
        let stats = ComponentTreeStats::new(inspector.root().create_child("cpu_stats")).await;
        stats
            .track_ready(
                ExtendedMoniker::ComponentInstance(vec!["a:0"].into()),
                FakeTask::new(
                    1,
                    vec![
                        zx::TaskRuntimeInfo {
                            cpu_time: 2,
                            queue_time: 4,
                            ..zx::TaskRuntimeInfo::default()
                        },
                        zx::TaskRuntimeInfo {
                            cpu_time: 6,
                            queue_time: 8,
                            ..zx::TaskRuntimeInfo::default()
                        },
                    ],
                ),
            )
            .await;

        stats.measure().await;

        assert_data_tree!(inspector, root: {
            cpu_stats: contains {
                measurements: contains {
                    components: {
                        "a": {
                            "1": {
                                "@samples": {
                                    "0": {
                                        timestamp: AnyProperty,
                                        cpu_time: 2i64,
                                        queue_time: 4i64,
                                    }
                                }
                            }
                        }
                    }
                }
            }
        });

        // Add another measurement
        stats.measure().await;

        assert_data_tree!(inspector, root: {
            cpu_stats: contains {
                measurements: contains {
                    components: {
                        "a": {
                            "1": {
                                "@samples": {
                                    "0": {
                                        timestamp: AnyProperty,
                                        cpu_time: 2i64,
                                        queue_time: 4i64,
                                    },
                                    "1": {
                                        timestamp: AnyProperty,
                                        cpu_time: 6i64,
                                        queue_time: 8i64,
                                    }
                                }
                            }
                        }
                    }
                }
            }
        });
    }

    #[fuchsia::test]
    async fn component_manager_stats_are_tracked() {
        // Set up the test
        let test = RoutingTest::new(
            "root",
            vec![("root", ComponentDeclBuilder::new().add_eager_child("a").build())],
        )
        .await;

        let koid =
            fuchsia_runtime::job_default().basic_info().expect("got basic info").koid.raw_koid();

        assert_data_tree!(test.builtin_environment.inspector, root: contains {
            cpu_stats: contains {
                measurements: contains {
                    components: contains {
                        "<component_manager>": {
                            koid.to_string() => {
                                "@samples": {
                                    "0": {
                                        cpu_time: AnyProperty,
                                        queue_time: AnyProperty,
                                        timestamp: AnyProperty,
                                    },
                                }
                            }
                        },
                    },
                },
            },
        });
    }

    #[fuchsia::test]
    async fn on_started_handles_parent_task() {
        let inspector = inspect::Inspector::new();
        let stats =
            Arc::new(ComponentTreeStats::new(inspector.root().create_child("cpu_stats")).await);
        let parent_task = FakeTask::new(
            1,
            vec![
                zx::TaskRuntimeInfo {
                    cpu_time: 20,
                    queue_time: 40,
                    ..zx::TaskRuntimeInfo::default()
                },
                zx::TaskRuntimeInfo {
                    cpu_time: 60,
                    queue_time: 80,
                    ..zx::TaskRuntimeInfo::default()
                },
            ],
        );
        let component_task = FakeTask::new(
            2,
            vec![
                zx::TaskRuntimeInfo {
                    cpu_time: 2,
                    queue_time: 4,
                    ..zx::TaskRuntimeInfo::default()
                },
                zx::TaskRuntimeInfo {
                    cpu_time: 6,
                    queue_time: 8,
                    ..zx::TaskRuntimeInfo::default()
                },
            ],
        );
        let fake_runtime =
            FakeRuntime::new(FakeDiagnosticsContainer::new(parent_task.clone(), None));
        stats
            .on_component_started(
                ExtendedMoniker::ComponentInstance(vec!["parent:0"].into()),
                &fake_runtime,
            )
            .await;

        let fake_runtime =
            FakeRuntime::new(FakeDiagnosticsContainer::new(component_task, Some(parent_task)));
        stats
            .on_component_started(
                ExtendedMoniker::ComponentInstance(vec!["child:0"].into()),
                &fake_runtime,
            )
            .await;

        // Wait for diagnostics data to be received since it's done in a non-blocking way on
        // started.
        loop {
            let mut ready_count: usize = 0;
            for stat in stats.tree.lock().await.values() {
                if stat.lock().await.is_measuring() {
                    ready_count += 1;
                }
            }
            if ready_count == 2 {
                break;
            }
            fasync::Timer::new(fasync::Time::after(100i64.millis())).await;
        }

        assert_data_tree!(inspector, root: {
            cpu_stats: contains {
                measurements: contains {
                    components: {
                        "parent": {
                            "1": {
                                "@samples": {
                                    // Taken when this task started.
                                    "0": {
                                        timestamp: AnyProperty,
                                        cpu_time: 20i64,
                                        queue_time: 40i64,
                                    },
                                }
                            },
                        },
                        "child": {
                            "2": {
                                "@samples": {
                                    "0": {
                                        timestamp: AnyProperty,
                                        cpu_time: 2i64,
                                        queue_time: 4i64,
                                    }
                                }
                            }
                        }
                    }
                }
            }
        });
    }

    #[fuchsia::test]
    async fn child_tasks_garbage_collection() {
        let inspector = inspect::Inspector::new();
        let stats =
            Arc::new(ComponentTreeStats::new(inspector.root().create_child("cpu_stats")).await);
        let parent_task = FakeTask::new(
            1,
            vec![
                zx::TaskRuntimeInfo {
                    cpu_time: 20,
                    queue_time: 40,
                    ..zx::TaskRuntimeInfo::default()
                },
                zx::TaskRuntimeInfo {
                    cpu_time: 60,
                    queue_time: 80,
                    ..zx::TaskRuntimeInfo::default()
                },
            ],
        );
        let component_task = FakeTask::new(
            2,
            vec![zx::TaskRuntimeInfo {
                cpu_time: 2,
                queue_time: 4,
                ..zx::TaskRuntimeInfo::default()
            }],
        );
        let fake_runtime =
            FakeRuntime::new(FakeDiagnosticsContainer::new(parent_task.clone(), None));
        stats
            .on_component_started(
                ExtendedMoniker::ComponentInstance(vec!["parent:0"].into()),
                &fake_runtime,
            )
            .await;

        let child_moniker = ExtendedMoniker::ComponentInstance(vec!["child:0"].into());
        let fake_runtime =
            FakeRuntime::new(FakeDiagnosticsContainer::new(component_task, Some(parent_task)));
        stats.on_component_started(child_moniker.clone(), &fake_runtime).await;

        // Wait for diagnostics data to be received since it's done in a non-blocking way on
        // started.
        loop {
            let mut ready_count: usize = 0;
            for stat in stats.tree.lock().await.values() {
                if stat.lock().await.is_measuring() {
                    ready_count += 1;
                }
            }
            if ready_count == 2 {
                break;
            }
            fasync::Timer::new(fasync::Time::after(100i64.millis())).await;
        }

        assert_eq!(stats.tree.lock().await.len(), 2);
        assert_eq!(stats.tasks.lock().await.len(), 2);

        // Invalidate the handle, to simulate that the component stopped.
        for task in stats.tree.lock().await.get(&child_moniker).unwrap().lock().await.tasks_mut() {
            task.lock().await.task_mut().invalid_handle = true;
        }

        for _ in 0..COMPONENT_CPU_MAX_SAMPLES {
            stats.measure().await;
        }

        stats.measure().await;

        // Child is gone and only the parent exists now.
        assert!(stats.tree.lock().await.get(&child_moniker).is_none());
        assert_eq!(stats.tree.lock().await.len(), 1);
        assert_eq!(stats.tasks.lock().await.len(), 1);
    }

    fn get_total_property(hierarchy: &DiagnosticsHierarchy, index: usize, property: &str) -> i64 {
        *hierarchy
            .get_property_by_path(&vec!["cpu_stats", "@total", &index.to_string(), property])
            .unwrap()
            .int()
            .unwrap()
    }

    fn get_recent_property(hierarchy: &DiagnosticsHierarchy, name: &str) -> i64 {
        *hierarchy
            .get_property_by_path(&vec!["cpu_stats", "recent_usage", name])
            .unwrap()
            .int()
            .unwrap()
    }
}
