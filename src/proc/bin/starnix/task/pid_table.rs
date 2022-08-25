// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::sync::{Arc, Weak};

use crate::task::*;
use crate::types::*;

/// Entities identified by a pid.
#[derive(Default)]
struct PidEntry {
    task: Option<Weak<Task>>,
    group: Option<Weak<ThreadGroup>>,
    process_group: Option<Weak<ProcessGroup>>,
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
        self.table.entry(pid).or_insert_with(Default::default)
    }

    pub fn allocate_pid(&mut self) -> pid_t {
        // TODO: wrap the pid number and check for collisions
        self.last_pid += 1;
        self.last_pid
    }

    pub fn get_task(&self, pid: pid_t) -> Option<Arc<Task>> {
        self.get_entry(pid).and_then(|entry| entry.task.as_ref()).and_then(|task| task.upgrade())
    }

    pub fn add_task(&mut self, task: &Arc<Task>) {
        let entry = self.get_entry_mut(task.id);
        assert!(entry.task.is_none());
        self.get_entry_mut(task.id).task = Some(Arc::downgrade(task));
    }

    pub fn remove_task(&mut self, pid: pid_t) {
        let entry = self.get_entry_mut(pid);
        assert!(entry.task.is_some());
        entry.task = None;
    }

    pub fn get_thread_groups(&self) -> Vec<Arc<ThreadGroup>> {
        self.table
            .iter()
            .flat_map(|(_pid, entry)| entry.group.as_ref().and_then(|g| g.upgrade()))
            .collect()
    }

    pub fn add_thread_group(&mut self, thread_group: &Arc<ThreadGroup>) {
        let entry = self.get_entry_mut(thread_group.leader);
        assert!(entry.group.is_none());
        entry.group = Some(Arc::downgrade(thread_group));
    }

    pub fn remove_thread_group(&mut self, pid: pid_t) {
        let entry = self.get_entry_mut(pid);
        assert!(entry.group.is_some());
        entry.group = None;
    }

    pub fn get_process_group(&self, pid: pid_t) -> Option<Arc<ProcessGroup>> {
        self.get_entry(pid)
            .and_then(|entry| entry.process_group.as_ref())
            .and_then(|process_group| process_group.upgrade())
    }

    pub fn add_process_group(&mut self, process_group: &Arc<ProcessGroup>) {
        let entry = self.get_entry_mut(process_group.leader);
        assert!(entry.process_group.is_none());
        entry.process_group = Some(Arc::downgrade(process_group));
    }

    pub fn remove_process_group(&mut self, pid: pid_t) {
        let entry = self.get_entry_mut(pid);
        assert!(entry.process_group.is_some());
        entry.process_group = None;
    }

    /// Returns the process ids for all the currently running processes.
    pub fn process_ids(&self) -> Vec<pid_t> {
        self.table.iter().flat_map(|(pid, entry)| entry.group.as_ref().and(Some(*pid))).collect()
    }
}
