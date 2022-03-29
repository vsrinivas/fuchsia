// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::sync::{Arc, Weak};

use crate::auth::*;
use crate::task::*;
use crate::types::*;

/// Entities identified by a pid. In the future this will include process groups and sessions.
#[derive(Default)]
struct PidEntry {
    task: Option<Weak<Task>>,
    group: Option<Weak<ThreadGroup>>,
    process_group: Option<ProcessGroup>,
    session: Option<Session>,
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

    pub fn get_thread_group(&self, pid: pid_t) -> Option<Arc<ThreadGroup>> {
        self.get_entry(pid).and_then(|entry| entry.group.as_ref()).and_then(|group| group.upgrade())
    }

    pub fn get_process_group(&self, pid: pid_t) -> Option<&ProcessGroup> {
        self.get_entry(pid).and_then(|entry| entry.process_group.as_ref())
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
        self.register_job_control(thread_group.leader, &thread_group.job_control.read());
    }

    pub fn set_job_control(&mut self, pid: pid_t, job_control: &ShellJobControl) {
        let group = self.get_thread_group(pid).unwrap();
        self.cleanup_job_control(pid, &group.as_ref().job_control.read());
        self.register_job_control(pid, job_control);
    }

    pub fn remove_task(&mut self, pid: pid_t) {
        let entry = self.get_entry_mut(pid);
        assert!(entry.task.is_some());
        entry.task = None;
    }

    pub fn remove_thread_group(&mut self, pid: pid_t) {
        let entry = self.get_entry_mut(pid);
        let group = entry.group.take().unwrap().upgrade();
        self.cleanup_job_control(pid, &group.unwrap().job_control.read());
    }

    /// Returns the task ids for all the currently running tasks.
    pub fn task_ids(&self) -> Vec<pid_t> {
        self.table.iter().flat_map(|(pid, entry)| entry.task.as_ref().and(Some(*pid))).collect()
    }

    fn cleanup_job_control(&mut self, pid: pid_t, job_control: &ShellJobControl) {
        let remove_process_group;
        {
            let process_group_entry = self.get_entry_mut(job_control.pgid);
            assert!(process_group_entry.process_group.is_some());
            remove_process_group = process_group_entry.process_group.as_mut().unwrap().remove(pid);
            if remove_process_group {
                process_group_entry.process_group = None;
            }
        }
        if remove_process_group {
            let session_entry = self.get_entry_mut(job_control.sid);
            assert!(session_entry.session.is_some());
            if session_entry.session.as_mut().unwrap().remove(job_control.pgid) {
                session_entry.session = None;
            }
        }
    }

    fn register_job_control(&mut self, pid: pid_t, job_control: &ShellJobControl) {
        {
            let process_group_entry = self.get_entry_mut(job_control.pgid);
            match &process_group_entry.process_group {
                None => {
                    assert!(pid == job_control.pgid);
                    process_group_entry.process_group =
                        Some(ProcessGroup::new(job_control.sid, job_control.pgid));
                }

                Some(process_group) => {
                    assert!(process_group.sid == job_control.sid);
                    process_group.tasks.write().insert(pid);
                }
            };
        }
        {
            let session_entry = self.get_entry_mut(job_control.sid);
            match &session_entry.session {
                None => {
                    assert!(job_control.sid == job_control.pgid);
                    assert!(pid == job_control.sid);
                    session_entry.session = Some(Session::new(job_control.sid));
                }
                Some(session) => {
                    session.process_groups.write().insert(job_control.pgid);
                }
            };
        }
    }
}
