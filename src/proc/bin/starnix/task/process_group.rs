// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::RwLock;
use std::collections::HashSet;

use crate::types::*;

pub struct ProcessGroup {
    /// The session of the process group.
    pub sid: pid_t,

    /// The leader of the process group.
    pub leader: pid_t,

    /// The tasks in the process group.
    pub tasks: RwLock<HashSet<pid_t>>,
}

impl PartialEq for ProcessGroup {
    fn eq(&self, other: &Self) -> bool {
        self.leader == other.leader
    }
}

impl ProcessGroup {
    pub fn new(sid: pid_t, leader: pid_t) -> ProcessGroup {
        let mut tasks = HashSet::new();
        tasks.insert(leader);

        ProcessGroup { sid, leader, tasks: RwLock::new(tasks) }
    }

    /// Removes the task from the session. Returns whether the process group is empty.
    pub fn remove(&mut self, pid: pid_t) -> bool {
        let mut tasks = self.tasks.write();
        tasks.remove(&pid);
        return tasks.is_empty();
    }
}
