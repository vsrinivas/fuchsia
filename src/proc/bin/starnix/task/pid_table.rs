// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::sync::{Arc, Weak};

use crate::task::*;
use crate::types::*;

/// Entities identified by a pid. In the future this will include process groups and sessions.
#[derive(Default)]
struct PidEntry {
    task: Option<Weak<Task>>,
    group: Option<Weak<ThreadGroup>>,
}

pub struct PidTable {
    /// The most-recently allocated pid in this table.
    last_pid: pid_t,

    /// The tasks in this table, organized by pid_t.
    table: HashMap<pid_t, PidEntry>,
}

impl PidTable {
    pub fn new() -> PidTable {
        PidTable { last_pid: 0, table: HashMap::new() }
    }

    fn get_entry(&self, pid: pid_t) -> Option<&PidEntry> {
        self.table.get(&pid)
    }
    fn get_entry_mut(&mut self, pid: pid_t) -> &mut PidEntry {
        self.table.entry(pid).or_insert(Default::default())
    }

    pub fn get_task(&self, pid: pid_t) -> Option<Arc<Task>> {
        self.get_entry(pid).and_then(|entry| entry.task.as_ref()).and_then(|task| task.upgrade())
    }

    pub fn get_thread_groups(&self) -> Vec<Arc<ThreadGroup>> {
        self.table
            .iter()
            .flat_map(|(_pid, entry)| entry.group.as_ref().and_then(|g| g.upgrade()))
            .collect()
    }

    pub fn allocate_pid(&mut self) -> pid_t {
        // TODO: wrap the pid number and check for collisions
        self.last_pid += 1;
        return self.last_pid;
    }

    pub fn add_task(&mut self, task: &Arc<Task>) {
        let entry = self.get_entry_mut(task.id);
        assert!(entry.task.is_none());
        self.get_entry_mut(task.id).task = Some(Arc::downgrade(task));
    }

    pub fn add_thread_group(&mut self, thread_group: &Arc<ThreadGroup>) {
        let entry = self.get_entry_mut(thread_group.leader);
        assert!(entry.group.is_none());
        entry.group = Some(Arc::downgrade(thread_group));
    }

    pub fn remove_task(&mut self, pid: pid_t) {
        let entry = self.get_entry_mut(pid);
        assert!(entry.task.is_some());
        entry.task = None;
    }

    pub fn remove_thread_group(&mut self, pid: pid_t) {
        let entry = self.get_entry_mut(pid);
        assert!(entry.group.is_some());
        entry.group = None;
    }

    /// Returns the task ids for all the currently running tasks.
    pub fn task_ids(&self) -> Vec<pid_t> {
        self.table.iter().flat_map(|(pid, entry)| entry.task.as_ref().and(Some(*pid))).collect()
    }
}
