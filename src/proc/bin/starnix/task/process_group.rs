// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::RwLock;
use std::collections::HashSet;
use std::sync::Arc;

use crate::task::*;
use crate::types::*;

#[derive(Debug)]
pub struct ProcessGroup {
    /// The session of the process group.
    pub session: Arc<Session>,

    /// The leader of the process group.
    pub leader: pid_t,

    /// The tasks in the process group.
    pub thread_groups: RwLock<HashSet<pid_t>>,
}

impl PartialEq for ProcessGroup {
    fn eq(&self, other: &Self) -> bool {
        self.leader == other.leader
    }
}

impl ProcessGroup {
    pub fn new(session: Arc<Session>, leader: pid_t) -> Arc<ProcessGroup> {
        let mut thread_groups = HashSet::new();
        thread_groups.insert(leader);
        session.process_groups.write().insert(leader);

        Arc::new(ProcessGroup { session, leader, thread_groups: RwLock::new(thread_groups) })
    }

    /// Removes the thread group from the process group. Returns whether the process group is empty.
    pub fn remove(&self, pid: pid_t) -> bool {
        let mut thread_groups = self.thread_groups.write();
        thread_groups.remove(&pid);
        return thread_groups.is_empty();
    }
}
