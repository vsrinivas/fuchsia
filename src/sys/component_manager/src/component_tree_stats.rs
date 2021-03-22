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
    fuchsia_zircon::{self as zx, AsHandleRef, HandleBased, Task},
    futures::{lock::Mutex, FutureExt},
    log::warn,
    moniker::ExtendedMoniker,
    std::{
        collections::{BTreeMap, VecDeque},
        convert::{TryFrom, TryInto},
        ops::Deref,
        sync::{Arc, Weak},
        time::Duration,
    },
};

const CPU_SAMPLE_PERIOD_SECONDS: Duration = Duration::from_secs(60);
const COMPONENT_CPU_MAX_SAMPLES: usize = 60;

/// Provides stats for all components running in the system.
pub struct ComponentTreeStats {
    /// Map from a moniker of a component running in the system to its stats.
    tree: Mutex<BTreeMap<ExtendedMoniker, Arc<Mutex<ComponentStats>>>>,

    /// The root of the tree stats.
    node: inspect::Node,

    /// A histogram storing stats about the time it took to process the CPU stats measurements.
    processing_times: inspect::IntExponentialHistogramProperty,

    /// The task that takes CPU samples every minute.
    task: Mutex<Option<fasync::Task<()>>>,
}

impl ComponentTreeStats {
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

        let this = Arc::new(Self {
            tree: Mutex::new(Self::init_tree()),
            node,
            processing_times,
            task: Mutex::new(None),
        });

        let weak_self = Arc::downgrade(&this);
        *(this.task.lock().await) = Some(Self::spawn_measuring_task(weak_self.clone()));

        this.node.record_lazy_child("measurements", move || {
            let weak_self_clone = weak_self.clone();
            async move {
                let inspector = inspect::Inspector::new();
                if let Some(this) = weak_self_clone.upgrade() {
                    this.write_measurements(inspector.root()).await;
                }
                let stats_node = inspector.root().create_child("inspect_stats");
                inspector.write_stats_to(&stats_node);
                inspector.root().record(stats_node);
                Ok(inspector)
            }
            .boxed()
        });

        this
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "ComponentTreeStats",
            vec![EventType::Started, EventType::Stopped],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    fn init_tree() -> BTreeMap<ExtendedMoniker, Arc<Mutex<ComponentStats>>> {
        let mut tree = BTreeMap::new();
        match fuchsia_runtime::job_default().duplicate_handle(zx::Rights::SAME_RIGHTS) {
            Ok(job) => {
                let stats = ComponentStats::ready(ComponentTasks {
                    component_task: Some(DiagnosticsTask::Job(job)),
                    ..ComponentTasks::EMPTY
                });
                tree.insert(ExtendedMoniker::ComponentManager, Arc::new(Mutex::new(stats)));
            }
            Err(err) => {
                warn!(
                    "Failed to duplicate component manager job. Not tracking its own stats: {:?}",
                    err
                )
            }
        }
        tree
    }

    async fn write_measurements(&self, node: &inspect::Node) {
        for (moniker, stats) in self.tree.lock().await.iter() {
            let stats_guard = stats.lock().await;
            if stats_guard.is_ready() {
                let child = node.create_child(moniker.to_string());
                stats_guard.write_to_inspect(&child);
                node.record(child);
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

    fn spawn_measuring_task(weak_self: Weak<Self>) -> fasync::Task<()> {
        fasync::Task::spawn(async move {
            loop {
                fasync::Timer::new(CPU_SAMPLE_PERIOD_SECONDS).await;
                match weak_self.upgrade() {
                    None => break,
                    Some(this) => {
                        let start = zx::Time::get_monotonic();
                        let stats = this.tree.lock().await.values().cloned().collect::<Vec<_>>();
                        for stat in stats {
                            stat.lock().await.measure();
                        }
                        this.processing_times
                            .insert((zx::Time::get_monotonic() - start).into_nanos());
                    }
                }
            }
        })
    }
}

#[async_trait]
impl Hook for ComponentTreeStats {
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
            EventType::Stopped => {
                self.tree.lock().await.remove(&target_moniker);
            }
            _ => {}
        }
        Ok(())
    }
}

struct ComponentStats {
    parent_task: Option<TaskInfo>,
    component_task: Option<TaskInfo>,
    _task: Option<fasync::Task<()>>,
}

impl ComponentStats {
    /// Creates a new `ComponentStats` awaiting the component tasks not ready to take measurements
    /// yet.
    pub fn pending(task: fasync::Task<()>) -> Self {
        Self { parent_task: None, component_task: None, _task: Some(task) }
    }

    /// Creates a new `ComponentStats` and starts taking measurements.
    pub fn ready(tasks: ComponentTasks) -> Self {
        let mut this = Self {
            component_task: tasks.component_task.and_then(|task| task.try_into().ok()),
            parent_task: tasks.parent_task.and_then(|task| task.try_into().ok()),
            _task: None,
        };
        this.measure();
        this
    }

    /// Whether or not the stats are ready to provide measurements or not.
    pub fn is_ready(&self) -> bool {
        self.parent_task.is_some() || self.component_task.is_some()
    }

    /// Start reading CPU stats.
    pub fn start_measuring(&mut self, tasks: ComponentTasks) {
        self.component_task = tasks.component_task.and_then(|task| task.try_into().ok());
        self.parent_task = tasks.parent_task.and_then(|task| task.try_into().ok());
        self.measure();
    }

    /// Takes a runtime info measurement and records it. Drops old ones if the maximum amount is
    /// exceeded.
    pub fn measure(&mut self) {
        if let Some(task) = &mut self.component_task {
            task.measure();
        }

        // TODO(fxbug.dev/56570): use this.
        if let Some(_) = self.parent_task {}
    }

    /// Writes the stats to inspect under the given node.
    pub fn write_to_inspect(&self, node: &inspect::Node) {
        if let Some(task) = &self.component_task {
            task.write_inspect_to(&node)
        }
        // TODO(fxbug.dev/56570): use this.
        if let Some(_) = &self.component_task {}
    }
}

struct Measurement {
    timestamp: zx::Time,
    cpu_time: zx::Duration,
    queue_time: zx::Duration,
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

struct TaskInfo {
    koid: zx::Koid,
    task: DiagnosticsTask,
    measurements: MeasurementsQueue,
}

impl TaskInfo {
    pub fn measure(&mut self) {
        if let Ok(runtime_info) = self.get_runtime_info() {
            self.measurements.insert(runtime_info.into());
        }
    }

    pub fn write_inspect_to(&self, parent: &inspect::Node) {
        let node = parent.create_child(self.koid.raw_koid().to_string());
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

    fn get_runtime_info(&self) -> Result<zx::TaskRuntimeInfo, zx::Status> {
        match &self.task {
            DiagnosticsTask::Job(job) => job.get_runtime_info(),
            DiagnosticsTask::Process(process) => process.get_runtime_info(),
            DiagnosticsTask::Thread(thread) => thread.get_runtime_info(),
            TaskUnknown!() => {
                unreachable!("only jobs, threads and processes are tasks");
            }
        }
    }
}

impl TryFrom<DiagnosticsTask> for TaskInfo {
    type Error = zx::Status;
    fn try_from(task: DiagnosticsTask) -> Result<Self, Self::Error> {
        let info = match &task {
            DiagnosticsTask::Job(job) => job.basic_info(),
            DiagnosticsTask::Process(process) => process.basic_info(),
            DiagnosticsTask::Thread(thread) => thread.basic_info(),
            TaskUnknown!() => {
                unreachable!("only jobs, threads and processes are tasks");
            }
        }?;
        Ok(Self { koid: info.koid, task, measurements: MeasurementsQueue::new() })
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
        fuchsia_inspect::testing::{assert_inspect_tree, AnyProperty},
        moniker::AbsoluteMoniker,
    };

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
        test.bind_instance(&moniker).await.expect("bind instance b success");

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
                measurements: {
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
                    "/": {
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
                    "/a:0": {
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
                    inspect_stats: {
                        current_size: 4096u64,
                        maximum_size: 262144u64,
                        total_dynamic_children: 0u64,
                    }
                }
            },
            inspect_stats: {
                current_size: 4096u64,
                maximum_size: 262144u64,
                total_dynamic_children: 0u64,
            }
        });

        // Verify that after stopping the instance the data is gone.
        let component = test.model.look_up(&moniker).await.unwrap();
        ActionSet::register(component, StopAction::new()).await.expect("stopped");

        assert!(stats.tree.lock().await.get(&extended_moniker).is_none());
        assert_inspect_tree!(test.builtin_environment.inspector, root: {
            cpu_stats: {
                processing_times_ns: AnyProperty,
                measurements: {
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
                    "/": {
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
                    inspect_stats: {
                        current_size: 4096u64,
                        maximum_size: 262144u64,
                        total_dynamic_children: 0u64,
                    }
                }
            },
            inspect_stats: {
                current_size: 4096u64,
                maximum_size: 262144u64,
                total_dynamic_children: 0u64,
            }
        });

        // Verify that after restarting the instance the data is back.
        test.bind_instance(&moniker).await.expect("bind instance b success");
        assert_inspect_tree!(test.builtin_environment.inspector, root: {
            cpu_stats: {
                processing_times_ns: AnyProperty,
                measurements: {
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
                    "/": {
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
                    "/a:0": {
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
                    inspect_stats: {
                        current_size: 4096u64,
                        maximum_size: 262144u64,
                        total_dynamic_children: 0u64,
                    }
                }
            },
            inspect_stats: {
                current_size: 4096u64,
                maximum_size: 262144u64,
                total_dynamic_children: 0u64,
            }
        });
    }
}
