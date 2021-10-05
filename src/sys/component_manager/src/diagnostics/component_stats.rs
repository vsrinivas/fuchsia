// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{
        measurement::Measurement, runtime_stats_source::RuntimeStatsSource, task_info::TaskInfo,
    },
    fuchsia_async as fasync, fuchsia_inspect as inspect, fuchsia_zircon_sys as zx_sys,
    futures::lock::Mutex,
    injectable_time::MonotonicTime,
    std::sync::Arc,
};

/// Tracks the tasks associated to some component and provides utilities for measuring them.
pub struct ComponentStats<T: RuntimeStatsSource> {
    tasks: Vec<Arc<Mutex<TaskInfo<T, MonotonicTime>>>>,
    _task: Option<fasync::Task<()>>,
    is_measuring: bool,
}

impl<T: RuntimeStatsSource + Send + Sync> ComponentStats<T> {
    /// Creates a new `ComponentStats` awaiting the component tasks not ready to take measurements
    /// yet.
    pub fn pending(task: fasync::Task<()>) -> Self {
        Self { tasks: Vec::new(), _task: Some(task), is_measuring: false }
    }

    /// Creates a new `ComponentStats` and starts taking measurements.
    pub fn ready(task_info: TaskInfo<T, MonotonicTime>) -> Self {
        let ready_tasks = vec![Arc::new(Mutex::new(task_info))];
        Self { tasks: ready_tasks, is_measuring: true, _task: None }
    }

    /// Associate a task with this component.
    pub async fn add_task(&mut self, task: Arc<Mutex<TaskInfo<T, MonotonicTime>>>) {
        self.tasks.push(task);
        self.is_measuring = true;
    }

    /// A `ComponentStats` is alive when:
    /// - It has not started measuring yet: this means we are still waiting for the diagnostics
    ///   data to arrive from the runner, or
    /// - Any of its tasks are alive.
    pub async fn is_alive(&self) -> bool {
        let mut any_task_alive = false;
        for task in &self.tasks {
            if task.lock().await.is_alive() {
                any_task_alive = true;
            }
        }
        !self.is_measuring || any_task_alive
    }

    /// Takes a runtime info measurement and records it. Drops old ones if the maximum amount is
    /// exceeded.
    pub async fn measure(&mut self) -> Measurement {
        let mut result = Measurement::default();
        for task in self.tasks.iter_mut() {
            if let Some(measurement) = task.lock().await.measure_if_no_parent().await {
                result += measurement;
            }
        }
        result
    }

    /// Removes all tasks that are not alive. Returns the koids of the ones that were deleted.
    pub async fn clean_stale(&mut self) -> Vec<zx_sys::zx_koid_t> {
        let mut deleted_koids = vec![];
        let mut final_tasks = vec![];
        while let Some(task) = self.tasks.pop() {
            let (is_alive, koid) = {
                let task_guard = task.lock().await;
                (task_guard.is_alive(), task_guard.koid())
            };
            if is_alive {
                final_tasks.push(task);
            } else {
                deleted_koids.push(koid);
            }
        }
        self.tasks = final_tasks;
        deleted_koids
    }

    /// Writes the stats to inspect under the given node. Returns the number of tasks that were
    /// written.
    pub async fn record_to_node(&self, node: &inspect::Node) -> u64 {
        let mut task_count = 0;
        for task in &self.tasks {
            task.lock().await.record_to_node(&node);
            task_count += 1;
        }
        task_count
    }

    pub fn is_measuring(&self) -> bool {
        self.is_measuring
    }

    /// Get a reference to the tasks tracked by these stats.
    pub fn tasks(&self) -> &[Arc<Mutex<TaskInfo<T, MonotonicTime>>>] {
        &self.tasks
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
    pub fn tasks_mut(&mut self) -> &mut [Arc<Mutex<TaskInfo<T, MonotonicTime>>>] {
        &mut self.tasks
    }
}
