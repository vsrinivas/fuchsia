// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        model::error::ModelError,
        model::hooks::{
            Event, EventPayload, EventType, HasEventType, Hook, HooksRegistration, RuntimeInfo,
        },
    },
    async_trait::async_trait,
    fidl_fuchsia_diagnostics_types::{
        ComponentDiagnostics, ComponentTasks, Task as DiagnosticsTask, TaskUnknown,
    },
    fuchsia_async as fasync,
    fuchsia_inspect::{self as inspect, HistogramProperty},
    fuchsia_inspect_contrib::nodes::BoundedListNode,
    fuchsia_zircon::{self as zx, AsHandleRef, HandleBased, Task},
    fuchsia_zircon_sys as zx_sys,
    futures::{lock::Mutex, FutureExt},
    log::warn,
    moniker::{AbsoluteMoniker, ExtendedMoniker},
    std::{
        collections::{BTreeMap, VecDeque},
        ops::{AddAssign, Deref, DerefMut},
        sync::{Arc, Weak},
        time::Duration,
    },
};

const CPU_SAMPLE_PERIOD_SECONDS: Duration = Duration::from_secs(60);
const COMPONENT_CPU_MAX_SAMPLES: usize = 60;

/// Provides stats for all components running in the system.
pub struct ComponentTreeStats<T: RuntimeStatsSource> {
    /// Map from a moniker of a component running in the system to its stats.
    tree: Mutex<BTreeMap<ExtendedMoniker, Arc<Mutex<ComponentStats<T>>>>>,

    /// The root of the tree stats.
    node: inspect::Node,

    /// A histogram storing stats about the time it took to process the CPU stats measurements.
    processing_times: inspect::IntExponentialHistogramProperty,

    /// The task that takes CPU samples every minute.
    task: Mutex<Option<fasync::Task<()>>>,

    /// Aggregated CPU stats.
    totals: Mutex<AggregatedStats>,
}

impl<T: 'static + RuntimeStatsSource + Send> ComponentTreeStats<T> {
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

        let totals = AggregatedStats::new(node.create_child("@total"));
        let this = Arc::new(Self {
            tree: Mutex::new(BTreeMap::new()),
            node,
            processing_times,
            task: Mutex::new(None),
            totals: Mutex::new(totals),
        });

        let weak_self = Arc::downgrade(&this);
        *(this.task.lock().await) = Some(Self::spawn_measuring_task(weak_self.clone()));

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

        this
    }

    async fn track_ready(&self, moniker: ExtendedMoniker, source: T) {
        if let Ok(task_info) = TaskInfo::try_from(source) {
            let mut stats = ComponentStats::ready(task_info);
            stats.measure();
            self.tree.lock().await.insert(moniker.clone(), Arc::new(Mutex::new(stats)));
        }
    }

    async fn write_measurements_to_inspect(self: &Arc<Self>) -> inspect::Inspector {
        let inspector = inspect::Inspector::new();
        let components = inspector.root().create_child("components");
        let task_count = self.write_measurements(&components).await;
        inspector.root().record_uint("task_count", task_count);
        inspector.root().record(components);

        let stats_node = inspector.root().create_child("inspect_stats");
        inspector.write_stats_to(&stats_node);
        inspector.root().record(stats_node);

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
            if stats_guard.is_measuring {
                // TODO(fxbug.dev/73169): unify diagnostics and component manager monikers.
                let key = match moniker {
                    ExtendedMoniker::ComponentManager => moniker.to_string(),
                    ExtendedMoniker::ComponentInstance(m) => {
                        if *m == AbsoluteMoniker::root() {
                            "<root>".to_string()
                        } else {
                            m.to_string().replacen("/", "", 1)
                        }
                    }
                };
                let child = node.create_child(key);
                task_count += stats_guard.write_inspect_to(&child);
                node.record(child);
            }
        }
        task_count
    }

    async fn measure(self: &Arc<Self>) {
        let start = zx::Time::get_monotonic();

        // Copy the stats and release the lock.
        let stats =
            self.tree.lock().await.iter().map(|(k, v)| (k.clone(), v.clone())).collect::<Vec<_>>();
        let mut aggregated = Measurement::empty(start);
        let mut to_remove = Vec::new();
        for (moniker, stat) in stats.into_iter() {
            let mut stat_guard = stat.lock().await;
            aggregated += &stat_guard.measure();
            stat_guard.clean_stale();
            if !stat_guard.is_alive() {
                to_remove.push(moniker);
            }
        }

        // Lock the tree so that we ensure no modifications are made while we are deleting
        let mut stats = self.tree.lock().await;
        for moniker in to_remove {
            // Ensure that they are still not alive (if a component restarted it might as well
            // be alive again).
            if let Some(stat) = stats.get(&moniker) {
                if !stat.lock().await.is_alive() {
                    stats.remove(&moniker);
                }
            }
        }

        self.totals.lock().await.update(aggregated);
        self.processing_times.insert((zx::Time::get_monotonic() - start).into_nanos());
    }

    fn spawn_measuring_task(weak_self: Weak<Self>) -> fasync::Task<()> {
        fasync::Task::spawn(async move {
            loop {
                fasync::Timer::new(CPU_SAMPLE_PERIOD_SECONDS).await;
                match weak_self.upgrade() {
                    None => break,
                    Some(this) => {
                        this.measure().await;
                    }
                }
            }
        })
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

    /// Starts tracking component manage rown stats.
    pub async fn track_component_manager_stats(&self) {
        match fuchsia_runtime::job_default().duplicate_handle(zx::Rights::SAME_RIGHTS) {
            Ok(job) => {
                self.track_ready(ExtendedMoniker::ComponentManager, DiagnosticsTask::Job(job))
                    .await;
            }
            Err(err) => {
                warn!(
                    "Failed to duplicate component manager job. Not tracking its own stats: {:?}",
                    err
                )
            }
        }
    }

    async fn on_component_started(
        self: &Arc<Self>,
        moniker: ExtendedMoniker,
        runtime: &RuntimeInfo,
    ) {
        let mut tree_guard = self.tree.lock().await;
        if tree_guard.contains_key(&moniker) {
            return;
        }

        let mut receiver_guard = runtime.diagnostics_receiver.lock().await;
        if let Some(receiver) = receiver_guard.take() {
            let weak_self = Arc::downgrade(&self);
            let moniker_for_fut = moniker.clone();
            let task = fasync::Task::spawn(async move {
                if let Some(ComponentDiagnostics { tasks: Some(tasks), .. }) = receiver.await.ok() {
                    if let Some(this) = weak_self.upgrade() {
                        if let Some(stats) = this.tree.lock().await.get_mut(&moniker_for_fut) {
                            stats.lock().await.start_measuring(tasks);
                        }
                    }
                }
            });
            tree_guard.insert(moniker, Arc::new(Mutex::new(ComponentStats::pending(task))));
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
        let mut node_writer = self.node.create_entry();
        node_writer
            .create_int("timestamp", measurement.timestamp.into_nanos())
            .create_int("cpu_time", measurement.cpu_time.into_nanos())
            .create_int("queue_time", measurement.queue_time.into_nanos());
        self.previous_measurement = self.recent_measurement.take();
        self.recent_measurement = Some(measurement);
    }

    fn write_recents_to(&self, node: &inspect::Node) {
        if let Some(measurement) = &self.previous_measurement {
            node.record_int("previous_cpu_time", measurement.cpu_time.into_nanos());
            node.record_int("previous_queue_time", measurement.queue_time.into_nanos());
            node.record_int("previous_timestamp", measurement.timestamp.into_nanos());
        }
        if let Some(measurement) = &self.recent_measurement {
            node.record_int("recent_cpu_time", measurement.cpu_time.into_nanos());
            node.record_int("recent_queue_time", measurement.queue_time.into_nanos());
            node.record_int("recent_timestamp", measurement.timestamp.into_nanos());
        }
    }
}

struct ComponentStats<T: RuntimeStatsSource> {
    tasks: Vec<TaskInfo<T>>,
    _task: Option<fasync::Task<()>>,
    is_measuring: bool,
}

impl<T: RuntimeStatsSource> ComponentStats<T> {
    /// Creates a new `ComponentStats` awaiting the component tasks not ready to take measurements
    /// yet.
    pub fn pending(task: fasync::Task<()>) -> Self {
        Self { tasks: Vec::new(), _task: Some(task), is_measuring: false }
    }

    /// Creates a new `ComponentStats` and starts taking measurements.
    pub fn ready(task: TaskInfo<T>) -> Self {
        Self { tasks: vec![task], is_measuring: true, _task: None }
    }

    /// Takes a runtime info measurement and records it. Drops old ones if the maximum amount is
    /// exceeded.
    pub fn measure(&mut self) -> Measurement {
        let mut result = Measurement::default();
        for task in self.tasks.iter_mut() {
            task.measure().map(|measurement| {
                result += measurement;
            });
        }

        result
    }

    /// A `ComponentStats` is alive when:
    /// - It has not started measuring yet: this means we are still waiting for the diagnostics
    ///   data to arrive from the runner, or
    /// - Any of its tasks are alive.
    pub fn is_alive(&self) -> bool {
        !self.is_measuring || self.tasks.iter().any(|task| task.is_alive())
    }

    /// Removes all tasks that are not alive.
    pub fn clean_stale(&mut self) {
        self.tasks.retain(|task| task.is_alive());
    }

    /// Writes the stats to inspect under the given node. Returns the number of tasks that were
    /// written.
    pub fn write_inspect_to(&self, node: &inspect::Node) -> u64 {
        let mut task_count = 0;
        for task in &self.tasks {
            task.write_inspect_to(&node);
            task_count += 1;
        }
        task_count
    }

    #[cfg(test)]
    fn total_measurements(&self) -> usize {
        self.tasks.iter().map(|task| task.measurements.len()).sum()
    }
}

impl ComponentStats<DiagnosticsTask> {
    /// Start reading CPU stats.
    pub fn start_measuring(&mut self, tasks: ComponentTasks) {
        if let Some(task) = tasks.component_task.and_then(|task| TaskInfo::try_from(task).ok()) {
            self.tasks.push(task);
            self.measure();
        }
        // Still mark is_measuring as true, if we failed to convert to a TaskInfo it means the
        // component already died since we couldn't query its basic info so we should make this
        // true so at least we clean up.
        self.is_measuring = true;
    }
}

#[derive(Default)]
struct Measurement {
    timestamp: zx::Time,
    cpu_time: zx::Duration,
    queue_time: zx::Duration,
}

impl Measurement {
    fn empty(timestamp: zx::Time) -> Self {
        Self {
            timestamp,
            cpu_time: zx::Duration::from_nanos(0),
            queue_time: zx::Duration::from_nanos(0),
        }
    }
}

impl AddAssign<&Measurement> for Measurement {
    fn add_assign(&mut self, other: &Measurement) {
        *self = Self {
            timestamp: self.timestamp,
            cpu_time: self.cpu_time + other.cpu_time,
            queue_time: self.queue_time + other.queue_time,
        };
    }
}

impl From<zx::TaskRuntimeInfo> for Measurement {
    fn from(info: zx::TaskRuntimeInfo) -> Self {
        Self {
            timestamp: zx::Time::get_monotonic(),
            cpu_time: zx::Duration::from_nanos(info.cpu_time),
            queue_time: zx::Duration::from_nanos(info.queue_time),
        }
    }
}

struct TaskInfo<T: RuntimeStatsSource> {
    koid: zx_sys::zx_koid_t,
    task: T,
    measurements: MeasurementsQueue,
    should_drop_old_measurements: bool,
    post_invalidation_measurements: usize,
}

impl<T: RuntimeStatsSource> TaskInfo<T> {
    /// Creates a new `TaskInfo` from the given cpu stats provider.
    // Due to https://github.com/rust-lang/rust/issues/50133 we cannot just derive TryFrom on a
    // generic type given a collision with the blanket implementation.
    pub fn try_from(task: T) -> Result<Self, zx::Status> {
        Ok(Self {
            koid: task.koid()?,
            task,
            measurements: MeasurementsQueue::new(),
            should_drop_old_measurements: false,
            post_invalidation_measurements: 0,
        })
    }

    /// Take a new measurement. If the handle of this task is invalid, then it keeps track of how
    /// many measurements would have been done. When the maximum amount allowed is hit, then it
    /// drops the oldest measurement.
    pub fn measure(&mut self) -> Option<&Measurement> {
        if self.task.handle_is_invalid() {
            if self.should_drop_old_measurements {
                self.measurements.pop_front();
            } else {
                self.post_invalidation_measurements += 1;
                self.should_drop_old_measurements = self.post_invalidation_measurements
                    + self.measurements.len()
                    >= COMPONENT_CPU_MAX_SAMPLES;
            }
            return None;
        }
        if let Ok(runtime_info) = self.task.get_runtime_info() {
            let measurement = runtime_info.into();
            self.measurements.insert(measurement);
            return self.measurements.back();
        }
        None
    }

    /// A task is alive when:
    /// - Its handle is valid, or
    /// - There's at least one measurement saved.
    pub fn is_alive(&self) -> bool {
        return !self.task.handle_is_invalid() || !self.measurements.is_empty();
    }

    /// Writes the task measurements under the given inspect node `parent`.
    pub fn write_inspect_to(&self, parent: &inspect::Node) {
        let node = parent.create_child(self.koid.to_string());
        let samples = node.create_child("@samples");
        for (i, measurement) in self.measurements.iter().enumerate() {
            let child = samples.create_child(i.to_string());
            child.record_int("timestamp", measurement.timestamp.into_nanos());
            child.record_int("cpu_time", measurement.cpu_time.into_nanos());
            child.record_int("queue_time", measurement.queue_time.into_nanos());
            samples.record(child);
        }
        node.record(samples);
        parent.record(node);
    }
}

struct MeasurementsQueue {
    values: VecDeque<Measurement>,
}

impl Deref for MeasurementsQueue {
    type Target = VecDeque<Measurement>;
    fn deref(&self) -> &Self::Target {
        &self.values
    }
}

impl DerefMut for MeasurementsQueue {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.values
    }
}

impl MeasurementsQueue {
    pub fn new() -> Self {
        Self { values: VecDeque::new() }
    }

    pub fn insert(&mut self, measurement: Measurement) {
        self.values.push_back(measurement);
        while self.values.len() > COMPONENT_CPU_MAX_SAMPLES {
            self.values.pop_front();
        }
    }
}

pub trait RuntimeStatsSource {
    /// The koid of the Cpu stats source.
    fn koid(&self) -> Result<zx_sys::zx_koid_t, zx::Status>;
    /// Whether the handle backing up this source is invalid.
    fn handle_is_invalid(&self) -> bool;
    /// Provides the runtime info containing the stats.
    fn get_runtime_info(&self) -> Result<zx::TaskRuntimeInfo, zx::Status>;
}

impl RuntimeStatsSource for DiagnosticsTask {
    fn koid(&self) -> Result<zx_sys::zx_koid_t, zx::Status> {
        let info = match &self {
            DiagnosticsTask::Job(job) => job.basic_info(),
            DiagnosticsTask::Process(process) => process.basic_info(),
            DiagnosticsTask::Thread(thread) => thread.basic_info(),
            TaskUnknown!() => {
                unreachable!("only jobs, threads and processes are tasks");
            }
        }?;
        Ok(info.koid.raw_koid())
    }

    fn handle_is_invalid(&self) -> bool {
        match &self {
            DiagnosticsTask::Job(job) => job.as_handle_ref().is_invalid(),
            DiagnosticsTask::Process(process) => process.as_handle_ref().is_invalid(),
            DiagnosticsTask::Thread(thread) => thread.as_handle_ref().is_invalid(),
            TaskUnknown!() => {
                unreachable!("only jobs, threads and processes are tasks");
            }
        }
    }

    fn get_runtime_info(&self) -> Result<zx::TaskRuntimeInfo, zx::Status> {
        match &self {
            DiagnosticsTask::Job(job) => job.get_runtime_info(),
            DiagnosticsTask::Process(process) => process.get_runtime_info(),
            DiagnosticsTask::Thread(thread) => thread.get_runtime_info(),
            TaskUnknown!() => {
                unreachable!("only jobs, threads and processes are tasks");
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            actions::{ActionSet, StopAction},
            testing::{
                routing_test_helpers::RoutingTest,
                test_helpers::{component_decl_with_test_runner, ComponentDeclBuilder},
            },
        },
        diagnostics_hierarchy::DiagnosticsHierarchy,
        fuchsia_inspect::testing::{assert_inspect_tree, AnyProperty},
        moniker::AbsoluteMoniker,
    };

    #[derive(Default)]
    struct FakeTask {
        values: Arc<std::sync::Mutex<VecDeque<zx::TaskRuntimeInfo>>>,
        koid: zx_sys::zx_koid_t,
        invalid_handle: bool,
    }

    impl FakeTask {
        fn new(koid: zx_sys::zx_koid_t, values: Vec<zx::TaskRuntimeInfo>) -> Self {
            Self {
                koid,
                invalid_handle: false,
                values: Arc::new(std::sync::Mutex::new(values.into())),
            }
        }
    }

    impl RuntimeStatsSource for FakeTask {
        fn koid(&self) -> Result<zx_sys::zx_koid_t, zx::Status> {
            Ok(self.koid.clone())
        }
        fn handle_is_invalid(&self) -> bool {
            self.invalid_handle
        }
        fn get_runtime_info(&self) -> Result<zx::TaskRuntimeInfo, zx::Status> {
            Ok(self.values.lock().unwrap().pop_front().unwrap_or(zx::TaskRuntimeInfo::default()))
        }
    }

    #[fuchsia::test]
    async fn rotates_measurements_per_task() {
        // Set up test
        let mut task: TaskInfo<FakeTask> = TaskInfo::try_from(FakeTask::default()).unwrap();
        assert!(task.is_alive());

        // Take three measurements.
        task.measure();
        assert_eq!(task.measurements.len(), 1);
        task.measure();
        assert_eq!(task.measurements.len(), 2);
        task.measure();
        assert!(task.is_alive());
        assert_eq!(task.measurements.len(), 3);

        // Invalidate the handle
        task.task.invalid_handle = true;

        // Allow MAX-N (N=3 here) measurements to be taken until we start dropping.
        for i in 3..COMPONENT_CPU_MAX_SAMPLES {
            task.measure();
            assert_eq!(task.measurements.len(), 3);
            assert_eq!(task.post_invalidation_measurements, i - 2);
        }

        task.measure(); // 1 dropped, 2 left
        assert!(task.is_alive());
        assert_eq!(task.measurements.len(), 2);
        task.measure(); // 2 dropped, 1 left
        assert!(task.is_alive());
        assert_eq!(task.measurements.len(), 1);

        // Take one last measure.
        task.measure(); // 3 dropped, 0 left
        assert!(!task.is_alive());
        assert_eq!(task.measurements.len(), 0);
    }

    #[fuchsia::test]
    async fn components_are_deleted_when_all_tasks_are_gone() {
        let inspector = inspect::Inspector::new();
        let stats = ComponentTreeStats::new(inspector.root().create_child("cpu_stats")).await;
        let moniker: AbsoluteMoniker = vec!["a:0"].into();
        let moniker: ExtendedMoniker = moniker.into();
        stats.tree.lock().await.insert(
            moniker.clone(),
            Arc::new(Mutex::new(ComponentStats::ready(
                TaskInfo::try_from(FakeTask::default()).unwrap(),
            ))),
        );
        for _ in 0..=COMPONENT_CPU_MAX_SAMPLES {
            stats.measure().await;
        }
        assert_eq!(stats.tree.lock().await.len(), 1);
        assert_eq!(
            stats.tree.lock().await.get(&moniker).unwrap().lock().await.total_measurements(),
            COMPONENT_CPU_MAX_SAMPLES
        );

        // Invalidate the handle, to simulate that the component stopped.
        for task in stats.tree.lock().await.get(&moniker).unwrap().lock().await.tasks.iter_mut() {
            task.task.invalid_handle = true;
        }

        for i in 0..COMPONENT_CPU_MAX_SAMPLES {
            stats.measure().await;
            assert_eq!(
                stats.tree.lock().await.get(&moniker).unwrap().lock().await.total_measurements(),
                COMPONENT_CPU_MAX_SAMPLES - i,
            );
        }
        stats.measure().await;
        assert!(stats.tree.lock().await.get(&moniker).is_none());
        assert_eq!(stats.tree.lock().await.len(), 0);
    }

    #[fuchsia::test]
    async fn total_holds_sum_of_stats() {
        let inspector = inspect::Inspector::new();
        let stats = ComponentTreeStats::new(inspector.root().create_child("cpu_stats")).await;
        stats.tree.lock().await.insert(
            ExtendedMoniker::ComponentInstance(vec!["a:0"].into()),
            Arc::new(Mutex::new(ComponentStats::ready(
                TaskInfo::try_from(FakeTask::new(
                    1,
                    vec![
                        zx::TaskRuntimeInfo { cpu_time: 2, queue_time: 4 },
                        zx::TaskRuntimeInfo { cpu_time: 6, queue_time: 8 },
                    ],
                ))
                .unwrap(),
            ))),
        );
        stats.tree.lock().await.insert(
            ExtendedMoniker::ComponentInstance(vec!["b:0"].into()),
            Arc::new(Mutex::new(ComponentStats::ready(
                TaskInfo::try_from(FakeTask::new(
                    2,
                    vec![
                        zx::TaskRuntimeInfo { cpu_time: 1, queue_time: 3 },
                        zx::TaskRuntimeInfo { cpu_time: 5, queue_time: 7 },
                    ],
                ))
                .unwrap(),
            ))),
        );

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
        stats.tree.lock().await.insert(
            ExtendedMoniker::ComponentInstance(vec!["a:0"].into()),
            Arc::new(Mutex::new(ComponentStats::ready(
                TaskInfo::try_from(FakeTask::new(
                    1,
                    vec![
                        zx::TaskRuntimeInfo { cpu_time: 2, queue_time: 4 },
                        zx::TaskRuntimeInfo { cpu_time: 6, queue_time: 8 },
                    ],
                ))
                .unwrap(),
            ))),
        );
        stats.tree.lock().await.insert(
            ExtendedMoniker::ComponentInstance(vec!["b:0"].into()),
            Arc::new(Mutex::new(ComponentStats::ready(
                TaskInfo::try_from(FakeTask::new(
                    2,
                    vec![
                        zx::TaskRuntimeInfo { cpu_time: 1, queue_time: 3 },
                        zx::TaskRuntimeInfo { cpu_time: 5, queue_time: 7 },
                    ],
                ))
                .unwrap(),
            ))),
        );

        stats.measure().await;
        let hierarchy = inspect::reader::read(&inspector).await.expect("read inspect hierarchy");

        // Verify initially there's no second most recent measurement since we only
        // have the initial measurement written.
        assert_inspect_tree!(&hierarchy, root: contains {
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
        assert_inspect_tree!(&hierarchy, root: contains {
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
    async fn records_to_inspect_when_receiving_diagnostics() {
        // Set up the test
        let test = RoutingTest::new(
            "root",
            vec![
                ("root", ComponentDeclBuilder::new().add_eager_child("a").build()),
                ("a", component_decl_with_test_runner()),
            ],
        )
        .await;

        // Start the component "a".
        let moniker: AbsoluteMoniker = vec!["a:0"].into();
        let extended_moniker: ExtendedMoniker = moniker.clone().into();
        let stats = &test.builtin_environment.component_tree_stats;
        test.bind_instance(&moniker).await.expect("bind instance a success");

        // The test runner uses the default job just for testing purposes. We use its koid for
        // asserting here.
        let koid =
            fuchsia_runtime::job_default().basic_info().expect("got basic info").koid.raw_koid();

        // Wait for the diagnostics event to be received
        assert!(stats.tree.lock().await.get(&extended_moniker).is_some());

        // Verify the data written to inspect.
        assert_inspect_tree!(test.builtin_environment.inspector, root: {
            cpu_stats: {
                processing_times_ns: AnyProperty,
                "@total": {
                    "0": {
                        timestamp: AnyProperty,
                        cpu_time: AnyProperty,
                        queue_time: AnyProperty,
                    }
                },
                recent_usage: {
                    recent_cpu_time: AnyProperty,
                    recent_queue_time: AnyProperty,
                    recent_timestamp: AnyProperty,
                },
                measurements: {
                    components: {
                        "<component_manager>": {
                            koid.to_string() => {
                                "@samples": {
                                    "0": {
                                        timestamp: AnyProperty,
                                        cpu_time: AnyProperty,
                                        queue_time: AnyProperty,
                                    }
                                }
                            }
                        },
                        "<root>": {
                            koid.to_string() => {
                                "@samples": {
                                    "0": {
                                        timestamp: AnyProperty,
                                        cpu_time: AnyProperty,
                                        queue_time: AnyProperty,
                                    }
                                }
                            }
                        },
                        "a:0": {
                            koid.to_string() => {
                                "@samples": {
                                    "0": {
                                        timestamp: AnyProperty,
                                        cpu_time: AnyProperty,
                                        queue_time: AnyProperty,
                                    }
                                }
                            }
                        }
                    },
                    inspect_stats: {
                        current_size: 4096u64,
                        maximum_size: 262144u64,
                        total_dynamic_children: 0u64,
                    },
                    task_count: 3u64
                }
            },
            inspect_stats: {
                current_size: 4096u64,
                maximum_size: 262144u64,
                total_dynamic_children: 0u64,
            }
        });

        // Verify that after stopping the instance the data is still there.
        let component = test.model.look_up(&moniker).await.unwrap();
        ActionSet::register(component, StopAction::new()).await.expect("stopped");

        assert!(stats.tree.lock().await.get(&extended_moniker).is_some());
        assert_inspect_tree!(test.builtin_environment.inspector, root: contains {
            cpu_stats: contains {
                measurements: contains {
                    components: contains {
                        "<component_manager>": {
                            koid.to_string() => {
                                "@samples": contains {
                                    "0": contains {
                                    }
                                }
                            }
                        },
                        "<root>": {
                            koid.to_string() => {
                                "@samples": contains {
                                    "0": contains {
                                    }
                                }
                            }
                        },
                        "a:0": {
                            koid.to_string() => contains {
                                "@samples": contains {
                                    "0": contains {
                                    }
                                }
                            }
                        }
                    }
                }
            }
        });

        // Verify that after invalidating and an mmediate restart the data stays there if
        // there was no measurement in between.
        for task in
            stats.tree.lock().await.get(&extended_moniker).unwrap().lock().await.tasks.iter_mut()
        {
            let job = zx::Job::from(zx::Handle::invalid());
            task.task = DiagnosticsTask::Job(job);
        }
        assert_inspect_tree!(test.builtin_environment.inspector, root: contains {
            cpu_stats: contains {
                measurements: contains {
                    components: contains {
                        "<component_manager>": {
                            koid.to_string() => {
                                "@samples": {
                                    "0": contains {
                                    }
                                }
                            }
                        },
                        "<root>": {
                            koid.to_string() => {
                                "@samples": {
                                    "0": contains {
                                    }
                                }
                            }
                        },
                        "a:0": {
                            koid.to_string() => contains {
                                "@samples": {
                                    "0": contains {
                                    }
                                }
                            }
                        }
                    }
                }
            }
        });

        // If a measurement happens, the single sample is now gone and the data is deleted.
        // For the rotation verification see previous tests.
        stats.measure().await;

        assert_inspect_tree!(test.builtin_environment.inspector, root: contains {
            cpu_stats: contains {
                measurements: contains {
                    components: contains {
                        "<component_manager>": {
                            koid.to_string() => {
                                "@samples": {
                                    "0": contains {
                                    },
                                    "1": contains {
                                    }
                                }
                            }
                        },
                        "<root>": {
                            koid.to_string() => {
                                "@samples": {
                                    "0": contains {
                                    },
                                    "1": contains {
                                    }
                                }
                            }
                        }
                    }
                }
            }
        });
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
