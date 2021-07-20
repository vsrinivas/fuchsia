// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{
        constants::COMPONENT_CPU_MAX_SAMPLES,
        measurement::{Measurement, MeasurementsQueue},
        runtime_stats_source::RuntimeStatsSource,
    },
    fuchsia_inspect as inspect, fuchsia_zircon as zx, fuchsia_zircon_sys as zx_sys,
    futures::{future::BoxFuture, lock::Mutex, FutureExt},
    lazy_static::lazy_static,
    std::sync::Weak,
};

lazy_static! {
    static ref SAMPLES: inspect::StringReference<'static> = "@samples".into();
    static ref SAMPLE_INDEXES: Vec<inspect::StringReference<'static>> =
        (0..COMPONENT_CPU_MAX_SAMPLES).map(|x| x.to_string().into()).collect();
}

#[derive(Debug)]
pub struct TaskInfo<T: RuntimeStatsSource> {
    koid: zx_sys::zx_koid_t,
    task: T,
    pub has_parent_task: bool,
    measurements: MeasurementsQueue,
    children: Vec<Weak<Mutex<TaskInfo<T>>>>,
    should_drop_old_measurements: bool,
    post_invalidation_measurements: usize,
}

impl<T: RuntimeStatsSource + Send + Sync> TaskInfo<T> {
    /// Creates a new `TaskInfo` from the given cpu stats provider.
    // Due to https://github.com/rust-lang/rust/issues/50133 we cannot just derive TryFrom on a
    // generic type given a collision with the blanket implementation.
    pub fn try_from(task: T) -> Result<Self, zx::Status> {
        Ok(Self {
            koid: task.koid()?,
            task,
            has_parent_task: false,
            measurements: MeasurementsQueue::new(),
            children: vec![],
            should_drop_old_measurements: false,
            post_invalidation_measurements: 0,
        })
    }

    /// Take a new measurement. If the handle of this task is invalid, then it keeps track of how
    /// many measurements would have been done. When the maximum amount allowed is hit, then it
    /// drops the oldest measurement.
    pub async fn measure_if_no_parent(&mut self) -> Option<&Measurement> {
        // Tasks with a parent are measured by the parent as done right below in the internal
        // `do_measure`.
        if self.has_parent_task {
            return None;
        }
        self.measure_subtree().await
    }

    /// Adds a weak pointer to a task for which this task is the parent.
    pub fn add_child(&mut self, task: Weak<Mutex<TaskInfo<T>>>) {
        self.children.push(task);
    }

    fn measure_subtree<'a>(&'a mut self) -> BoxFuture<'a, Option<&'a Measurement>> {
        async move {
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
            if let Ok(runtime_info) = self.task.get_runtime_info().await {
                let mut measurement = runtime_info.into();

                // Subtract all child measurements.
                let mut alive_children = vec![];
                while let Some(weak_child) = self.children.pop() {
                    if let Some(child) = weak_child.upgrade() {
                        let mut child_guard = child.lock().await;
                        if let Some(child_measurement) = child_guard.measure_subtree().await {
                            measurement -= child_measurement;
                        }
                        if child_guard.is_alive() {
                            alive_children.push(weak_child);
                        }
                    }
                }
                self.children = alive_children;

                self.measurements.insert(measurement);
                return self.measurements.back();
            }
            None
        }
        .boxed()
    }

    /// A task is alive when:
    /// - Its handle is valid, or
    /// - There's at least one measurement saved.
    pub fn is_alive(&self) -> bool {
        return !self.task.handle_is_invalid() || !self.measurements.is_empty();
    }

    /// Writes the task measurements under the given inspect node `parent`.
    pub fn record_to_node(&self, parent: &inspect::Node) {
        let node = parent.create_child(self.koid.to_string());
        let samples = node.create_child(&*SAMPLES);
        for (i, measurement) in self.measurements.iter().enumerate() {
            let child = samples.create_child(&SAMPLE_INDEXES[i]);
            measurement.record_to_node(&child);
            samples.record(child);
        }
        node.record(samples);
        parent.record(node);
    }

    pub fn koid(&self) -> zx_sys::zx_koid_t {
        self.koid
    }

    #[cfg(test)]
    pub fn total_measurements(&self) -> usize {
        self.measurements.len()
    }

    #[cfg(test)]
    pub fn task_mut(&mut self) -> &mut T {
        &mut self.task
    }
}

#[cfg(test)]
mod tests {
    use inspect::testing::DiagnosticsHierarchyGetter;

    use {
        super::*,
        crate::diagnostics::testing::FakeTask,
        fuchsia_inspect::testing::{assert_data_tree, AnyProperty},
        std::sync::Arc,
    };

    #[fuchsia::test]
    async fn rotates_measurements_per_task() {
        // Set up test
        let mut task: TaskInfo<FakeTask> = TaskInfo::try_from(FakeTask::default()).unwrap();
        assert!(task.is_alive());

        // Take three measurements.
        task.measure_if_no_parent().await;
        assert_eq!(task.measurements.len(), 1);
        task.measure_if_no_parent().await;
        assert_eq!(task.measurements.len(), 2);
        task.measure_if_no_parent().await;
        assert!(task.is_alive());
        assert_eq!(task.measurements.len(), 3);

        // Invalidate the handle
        task.task.invalid_handle = true;

        // Allow MAX-N (N=3 here) measurements to be taken until we start dropping.
        for i in 3..COMPONENT_CPU_MAX_SAMPLES {
            task.measure_if_no_parent().await;
            assert_eq!(task.measurements.len(), 3);
            assert_eq!(task.post_invalidation_measurements, i - 2);
        }

        task.measure_if_no_parent().await; // 1 dropped, 2 left
        assert!(task.is_alive());
        assert_eq!(task.measurements.len(), 2);
        task.measure_if_no_parent().await; // 2 dropped, 1 left
        assert!(task.is_alive());
        assert_eq!(task.measurements.len(), 1);

        // Take one last measure.
        task.measure_if_no_parent().await; // 3 dropped, 0 left
        assert!(!task.is_alive());
        assert_eq!(task.measurements.len(), 0);
    }

    #[fuchsia::test]
    async fn write_inspect() {
        let mut task = TaskInfo::try_from(FakeTask::new(
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
        ))
        .unwrap();

        task.measure_if_no_parent().await;
        task.measure_if_no_parent().await;

        let inspector = inspect::Inspector::new();
        task.record_to_node(inspector.root());
        assert_data_tree!(inspector, root: {
            "1": {
                "@samples": {
                    "0": {
                        cpu_time: 2i64,
                        queue_time: 4i64,
                        timestamp: AnyProperty,
                    },
                    "1": {
                        cpu_time: 6i64,
                        queue_time: 8i64,
                        timestamp: AnyProperty,
                    }
                }
            }
        });
    }

    #[fuchsia::test]
    async fn write_more_than_max_samples() {
        let inspector = inspect::Inspector::new();
        let mut task = TaskInfo::try_from(FakeTask::new(
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
        ))
        .unwrap();

        for _ in 0..(COMPONENT_CPU_MAX_SAMPLES + 10) {
            assert!(task.measure_if_no_parent().await.is_some());
        }

        assert_eq!(task.measurements.len(), COMPONENT_CPU_MAX_SAMPLES);
        task.record_to_node(inspector.root());

        let hierarchy = inspector.get_diagnostics_hierarchy();
        for top_level in &hierarchy.children {
            for i in 0..COMPONENT_CPU_MAX_SAMPLES {
                let index = i.to_string();
                let child =
                    hierarchy.get_child_by_path(&[&top_level.name, "@samples", &index]).unwrap();
                assert_eq!(child.name, index);
            }
        }
    }

    #[fuchsia::test]
    async fn measure_with_children() {
        let mut task = TaskInfo::try_from(FakeTask::new(
            1,
            vec![
                zx::TaskRuntimeInfo {
                    cpu_time: 100,
                    queue_time: 200,
                    ..zx::TaskRuntimeInfo::default()
                },
                zx::TaskRuntimeInfo {
                    cpu_time: 300,
                    queue_time: 400,
                    ..zx::TaskRuntimeInfo::default()
                },
            ],
        ))
        .unwrap();

        let child_1 = Arc::new(Mutex::new(
            TaskInfo::try_from(FakeTask::new(
                2,
                vec![
                    zx::TaskRuntimeInfo {
                        cpu_time: 10,
                        queue_time: 20,
                        ..zx::TaskRuntimeInfo::default()
                    },
                    zx::TaskRuntimeInfo {
                        cpu_time: 30,
                        queue_time: 40,
                        ..zx::TaskRuntimeInfo::default()
                    },
                ],
            ))
            .unwrap(),
        ));

        let child_2 = Arc::new(Mutex::new(
            TaskInfo::try_from(FakeTask::new(
                3,
                vec![
                    zx::TaskRuntimeInfo {
                        cpu_time: 5,
                        queue_time: 2,
                        ..zx::TaskRuntimeInfo::default()
                    },
                    zx::TaskRuntimeInfo {
                        cpu_time: 15,
                        queue_time: 4,
                        ..zx::TaskRuntimeInfo::default()
                    },
                ],
            ))
            .unwrap(),
        ));

        task.add_child(Arc::downgrade(&child_1));
        task.add_child(Arc::downgrade(&child_2));

        {
            let measurement = task.measure_if_no_parent().await.unwrap();
            assert_eq!(measurement.cpu_time().into_nanos(), 100 - 10 - 5);
            assert_eq!(measurement.queue_time().into_nanos(), 200 - 20 - 2);
        }
        assert_eq!(child_1.lock().await.total_measurements(), 1);
        assert_eq!(child_2.lock().await.total_measurements(), 1);

        // Fake child 2 not being alive anymore. It should be removed.
        {
            let mut child_2_guard = child_2.lock().await;
            child_2_guard.task.invalid_handle = true;
            child_2_guard.measurements = MeasurementsQueue::new();
        }

        assert_eq!(task.children.len(), 2);
        {
            let measurement = task.measure_if_no_parent().await.unwrap();
            assert_eq!(measurement.cpu_time().into_nanos(), 300 - 30);
            assert_eq!(measurement.queue_time().into_nanos(), 400 - 40);
        }

        assert_eq!(task.children.len(), 1); // after measuring dead children are cleaned.
        assert_eq!(child_1.lock().await.total_measurements(), 2);
    }
}
