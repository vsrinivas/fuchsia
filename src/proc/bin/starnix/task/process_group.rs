// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::BTreeMap;
use std::sync::{Arc, Weak};

use crate::lock::RwLock;
use crate::mutable_state::*;
use crate::signals::*;
use crate::task::*;
use crate::types::*;

#[derive(Debug)]
pub struct ProcessGroupMutableState {
    /// The thread_groups in the process group.
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
    pub fn new(leader: pid_t, session: Option<Arc<Session>>) -> Arc<ProcessGroup> {
        let session = session.unwrap_or_else(|| Session::new(leader));
        let process_group = Arc::new(ProcessGroup {
            session: session.clone(),
            leader,
            mutable_state: RwLock::new(ProcessGroupMutableState { thread_groups: BTreeMap::new() }),
        });
        session.write().insert(&process_group);
        process_group
    }

    state_accessor!(ProcessGroup, mutable_state);

    pub fn insert<'a>(self: &Arc<Self>, thread_group: &impl ThreadGroupReadGuard<'a>) {
        self.write().insert(thread_group);
    }

    /// Removes the thread group from the process group. Returns whether the process group is empty.
    pub fn remove<'a>(self: &Arc<Self>, thread_group: &impl ThreadGroupReadGuard<'a>) -> bool {
        self.write().remove(thread_group)
    }

    pub fn send_signals(self: &Arc<Self>, signals: &[Signal]) {
        let thread_groups = self.read().thread_groups().collect::<Vec<_>>();
        for signal in signals.iter() {
            let unchecked_signal: UncheckedSignal = (*signal).into();
            let tasks = thread_groups
                .iter()
                .flat_map(|tg| tg.read().get_signal_target(&unchecked_signal))
                .collect::<Vec<_>>();
            for task in tasks {
                send_signal(&task, SignalInfo::default(*signal));
            }
        }
    }
}

state_implementation!(ProcessGroup, ProcessGroupMutableState, {
    pub fn thread_groups(&self) -> Box<dyn Iterator<Item = Arc<ThreadGroup>> + '_> {
        Box::new(self.thread_groups.values().map(|t| {
            t.upgrade()
                .expect("Weak references to thread_groups in ProcessGroup must always be valid")
        }))
    }

    fn insert<'a>(&mut self, thread_group: &impl ThreadGroupReadGuard<'a>) {
        self.thread_groups.insert(thread_group.leader(), Arc::downgrade(thread_group.base()));
    }

    /// Removes the thread group from the process group. Returns whether the process group is empty.
    fn remove<'a>(&mut self, thread_group: &impl ThreadGroupReadGuard<'a>) -> bool {
        self.thread_groups.remove(&thread_group.leader());

        self.thread_groups.is_empty()
    }
});
