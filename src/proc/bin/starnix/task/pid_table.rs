// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::sync::{Arc, Weak};

use crate::task::*;
use crate::types::*;

pub struct PidTable {
    /// The most-recently allocated pid in this table.
    last_pid: pid_t,

    /// The tasks in this table, organized by pid_t.
    ///
    /// This reference is the primary reference keeping the tasks alive.
    tasks: HashMap<pid_t, Weak<Task>>,

    /// The thread groups that are present in this table.
    thread_groups: HashMap<pid_t, Weak<ThreadGroup>>,
}

impl PidTable {
    pub fn new() -> PidTable {
        PidTable { last_pid: 0, tasks: HashMap::new(), thread_groups: HashMap::new() }
    }

    pub fn get_task(&self, pid: pid_t) -> Option<Arc<Task>> {
        self.tasks.get(&pid).and_then(|task| task.upgrade())
    }

    pub fn get_thread_groups(&self) -> Vec<Arc<ThreadGroup>> {
        self.thread_groups.iter().flat_map(|(_pid, thread_group)| thread_group.upgrade()).collect()
    }

    pub fn allocate_pid(&mut self) -> pid_t {
        self.last_pid += 1;
        return self.last_pid;
    }

    pub fn add_task(&mut self, task: &Arc<Task>) {
        assert!(!self.tasks.contains_key(&task.id));
        self.tasks.insert(task.id, Arc::downgrade(task));
    }

    pub fn add_thread_group(&mut self, thread_group: &Arc<ThreadGroup>) {
        assert!(!self.thread_groups.contains_key(&thread_group.leader));
        self.thread_groups.insert(thread_group.leader, Arc::downgrade(thread_group));
    }

    pub fn remove_task(&mut self, pid: pid_t) {
        self.tasks.remove(&pid);
    }

    pub fn remove_thread_group(&mut self, pid: pid_t) {
        self.thread_groups.remove(&pid);
    }

    /// Returns the task ids for all the currently running tasks.
    pub fn task_ids(&self) -> Vec<pid_t> {
        self.tasks.keys().cloned().collect()
    }
}
