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
};

pub struct TaskInfo<T: RuntimeStatsSource> {
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
    pub async fn measure(&mut self) -> Option<&Measurement> {
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
    pub fn record_to_node(&self, parent: &inspect::Node) {
        let node = parent.create_child(self.koid.to_string());
        let samples = node.create_child("@samples");
        for (i, measurement) in self.measurements.iter().enumerate() {
            let child = samples.create_child(i.to_string());
            measurement.record_to_node(&child);
            samples.record(child);
        }
        node.record(samples);
        parent.record(node);
    }

    #[cfg(test)]
    pub fn total_measurements(&self) -> usize {
        self.measurements.len()
    }

    #[cfg(test)]
    pub fn set_task(&mut self, task: T) {
        self.task = task;
    }

    #[cfg(test)]
    pub fn task_mut(&mut self) -> &mut T {
        &mut self.task
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::diagnostics::testing::FakeTask;
    use fuchsia_inspect::testing::{assert_inspect_tree, AnyProperty};

    #[fuchsia::test]
    async fn rotates_measurements_per_task() {
        // Set up test
        let mut task: TaskInfo<FakeTask> = TaskInfo::try_from(FakeTask::default()).unwrap();
        assert!(task.is_alive());

        // Take three measurements.
        task.measure().await;
        assert_eq!(task.measurements.len(), 1);
        task.measure().await;
        assert_eq!(task.measurements.len(), 2);
        task.measure().await;
        assert!(task.is_alive());
        assert_eq!(task.measurements.len(), 3);

        // Invalidate the handle
        task.task.invalid_handle = true;

        // Allow MAX-N (N=3 here) measurements to be taken until we start dropping.
        for i in 3..COMPONENT_CPU_MAX_SAMPLES {
            task.measure().await;
            assert_eq!(task.measurements.len(), 3);
            assert_eq!(task.post_invalidation_measurements, i - 2);
        }

        task.measure().await; // 1 dropped, 2 left
        assert!(task.is_alive());
        assert_eq!(task.measurements.len(), 2);
        task.measure().await; // 2 dropped, 1 left
        assert!(task.is_alive());
        assert_eq!(task.measurements.len(), 1);

        // Take one last measure.
        task.measure().await; // 3 dropped, 0 left
        assert!(!task.is_alive());
        assert_eq!(task.measurements.len(), 0);
    }

    #[fuchsia::test]
    async fn write_inspect() {
        let mut task = TaskInfo::try_from(FakeTask::new(
            1,
            vec![
                zx::TaskRuntimeInfo { cpu_time: 2, queue_time: 4 },
                zx::TaskRuntimeInfo { cpu_time: 6, queue_time: 8 },
            ],
        ))
        .unwrap();

        task.measure().await;
        task.measure().await;

        let inspector = inspect::Inspector::new();
        task.record_to_node(inspector.root());
        assert_inspect_tree!(inspector, root: {
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
}
