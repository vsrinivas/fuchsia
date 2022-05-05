// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::BTreeMap;
use std::sync::{Arc, Weak};

use crate::lock::RwLock;
use crate::mutable_state::*;
use crate::task::*;
use crate::types::*;

#[derive(Debug)]
pub struct ProcessGroupMutableState {
    /// The processes in the process group.
    ///
    /// The references to ThreadGroup is weak to prevent cycles as ThreadGroup have a Arc reference to their process group.
    /// It is still expected that these weak references are always valid, as thread groups must unregister
    /// themselves before they are deleted.
    thread_groups: BTreeMap<pid_t, Weak<ThreadGroup>>,
}

#[derive(Debug)]
pub struct ProcessGroup {
    /// The session of the process group.
    pub session: Arc<Session>,

    /// The leader of the process group.
    pub leader: pid_t,

    /// The mutable state of the ProcessGroup.
    mutable_state: RwLock<ProcessGroupMutableState>,
}

impl PartialEq for ProcessGroup {
    fn eq(&self, other: &Self) -> bool {
        self.leader == other.leader
    }
}

impl ProcessGroup {
    pub fn new(session: Arc<Session>, leader: pid_t) -> Arc<ProcessGroup> {
        let process_group = Arc::new(ProcessGroup {
            session: session.clone(),
            leader,
            mutable_state: RwLock::new(ProcessGroupMutableState { thread_groups: BTreeMap::new() }),
        });
        session.write().insert(&process_group);
        process_group
    }

    state_accessor!(ProcessGroup, mutable_state);
}

state_implementation!(ProcessGroup, ProcessGroupMutableState, {
    pub fn thread_groups(&self) -> Box<dyn Iterator<Item = Arc<ThreadGroup>> + '_> {
        Box::new(self.thread_groups.values().map(|t| {
            t.upgrade().expect("Weak references to processes in ProcessGroup must always be valid")
        }))
    }

    pub fn insert(&mut self, thread_group: &Arc<ThreadGroup>) {
        self.thread_groups.insert(thread_group.leader, Arc::downgrade(thread_group));
    }

    /// Removes the thread group from the process group. Returns whether the process group is empty.
    pub fn remove(&mut self, pid: pid_t) -> bool {
        self.thread_groups.remove(&pid);
        self.thread_groups.is_empty()
    }
});
