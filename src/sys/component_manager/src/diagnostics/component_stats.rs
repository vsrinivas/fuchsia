// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{
        measurement::Measurement, runtime_stats_source::RuntimeStatsSource, task_info::TaskInfo,
    },
    fidl_fuchsia_diagnostics_types::{ComponentTasks, Task},
    fuchsia_async as fasync, fuchsia_inspect as inspect,
    futures::future::join_all,
};

pub struct ComponentStats<T: RuntimeStatsSource> {
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
    pub async fn measure(&mut self) -> Measurement {
        let measurements = join_all(self.tasks.iter_mut().map(|task| task.measure())).await;
        let mut result = Measurement::default();
        for measurement in measurements {
            if let Some(m) = measurement {
                result += m;
            }
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
    pub fn record_to_node(&self, node: &inspect::Node) -> u64 {
        let mut task_count = 0;
        for task in &self.tasks {
            task.record_to_node(&node);
            task_count += 1;
        }
        task_count
    }

    pub fn is_measuring(&self) -> bool {
        self.is_measuring
    }

    #[cfg(test)]
    pub fn tasks_mut(&mut self) -> &mut [TaskInfo<T>] {
        &mut self.tasks
    }

    #[cfg(test)]
    pub fn total_measurements(&self) -> usize {
        self.tasks.iter().map(|task| task.total_measurements()).sum()
    }
}

impl ComponentStats<Task> {
    /// Start reading CPU stats.
    pub async fn start_measuring(&mut self, tasks: ComponentTasks) {
        if let Some(task) = tasks.component_task.and_then(|task| TaskInfo::try_from(task).ok()) {
            self.tasks.push(task);
            self.measure().await;
        }
        // Still mark is_measuring as true, if we failed to convert to a TaskInfo it means the
        // component already died since we couldn't query its basic info so we should make this
        // true so at least we clean up.
        self.is_measuring = true;
    }
}
