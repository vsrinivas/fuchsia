// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::cpu::{
        measurement::Measurement, runtime_stats_source::RuntimeStatsSource, task_info::TaskInfo,
    },
    fuchsia_inspect as inspect, fuchsia_zircon as zx,
    futures::lock::Mutex,
    std::{fmt::Debug, sync::Arc},
};

/// Tracks the tasks associated to some component and provides utilities for measuring them.
pub struct ComponentStats<T: RuntimeStatsSource + Debug> {
    tasks: Vec<Arc<Mutex<TaskInfo<T>>>>,
}

impl<T: 'static + RuntimeStatsSource + Debug + Send + Sync> ComponentStats<T> {
    /// Creates a new `ComponentStats` and starts taking measurements.
    pub fn new() -> Self {
        Self { tasks: vec![] }
    }

    /// Associate a task with this component.
    pub async fn add_task(&mut self, task: Arc<Mutex<TaskInfo<T>>>) {
        self.tasks.push(task);
    }

    /// A `ComponentStats` is alive when:
    /// - It has not started measuring yet: this means we are still waiting for the diagnostics
    ///   data to arrive from the runner, or
    /// - Any of its tasks are alive.
    pub async fn is_alive(&self) -> bool {
        let mut any_task_alive = false;
        for task in &self.tasks {
            if task.lock().await.is_alive().await {
                any_task_alive = true;
            }
        }
        any_task_alive
    }

    /// Takes a runtime info measurement and records it. Drops old ones if the maximum amount is
    /// exceeded.
    ///
    /// Applies to tasks which have TaskState::Alive or TaskState::Terminated.
    pub async fn measure(&mut self) -> Measurement {
        let mut result = Measurement::default();
        for task in self.tasks.iter_mut() {
            if let Some(measurement) = task.lock().await.measure_if_no_parent().await {
                result += measurement;
            }
        }

        result
    }

    /// This produces measurements for tasks which have TaskState::TerminatedAndMeasured
    /// but also have measurement data for the past hour.
    pub async fn measure_tracked_dead_tasks(&self) -> Measurement {
        let mut result = Measurement::default();

        for task in self.tasks.iter() {
            let locked_task = task.lock().await;

            // this implies that `clean_stale()` will take the measurement
            if locked_task.measurements.no_true_measurements() {
                continue;
            }

            if let Some(m) = locked_task.exited_cpu().await {
                result += m;
            }
        }

        result
    }

    /// Removes all tasks that are not alive.
    ///
    /// Returns the koids of the ones that were deleted and the sum of the final measurements
    /// of the dead tasks. The measurement produced is of Tasks with
    /// TaskState::TerminatedAndMeasured.
    pub async fn clean_stale(&mut self) -> (Vec<zx::sys::zx_koid_t>, Measurement) {
        let mut deleted_koids = vec![];
        let mut final_tasks = vec![];
        let mut exited_cpu_time = Measurement::default();
        while let Some(task) = self.tasks.pop() {
            let (is_alive, koid) = {
                let task_guard = task.lock().await;
                (task_guard.is_alive().await, task_guard.koid())
            };

            if is_alive {
                final_tasks.push(task);
            } else {
                deleted_koids.push(koid);
                let locked_task = task.lock().await;
                if let Some(m) = locked_task.exited_cpu().await {
                    exited_cpu_time += m;
                }
            }
        }
        self.tasks = final_tasks;
        (deleted_koids, exited_cpu_time)
    }

    pub async fn remove_by_koids(&mut self, remove: &[zx::sys::zx_koid_t]) {
        let mut final_tasks = vec![];
        while let Some(task) = self.tasks.pop() {
            let task_koid = task.lock().await.koid();
            if !remove.contains(&task_koid) {
                final_tasks.push(task)
            }
        }

        self.tasks = final_tasks;
    }

    pub async fn gather_dead_tasks(&self) -> Vec<(zx::Time, Arc<Mutex<TaskInfo<T>>>)> {
        let mut dead_tasks = vec![];
        for task in &self.tasks {
            if let Some(t) = task.lock().await.most_recent_measurement().await {
                dead_tasks.push((t, task.clone()));
            }
        }

        dead_tasks
    }

    /// Writes the stats to inspect under the given node. Returns the number of tasks that were
    /// written.
    pub async fn record_to_node(&self, node: &inspect::Node) -> u64 {
        for task in &self.tasks {
            task.lock().await.record_to_node(&node);
        }
        self.tasks.len() as u64
    }

    #[cfg(test)]
    pub async fn total_measurements(&self) -> usize {
        let mut sum = 0;
        for task in &self.tasks {
            sum += task.lock().await.total_measurements();
        }
        sum
    }

    #[cfg(test)]
    pub fn tasks_mut(&mut self) -> &mut [Arc<Mutex<TaskInfo<T>>>] {
        &mut self.tasks
    }
}
