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

    /// Whether this process group is orphaned and already notified its members.
    orphaned: bool,
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

impl Eq for ProcessGroup {}

impl std::hash::Hash for ProcessGroup {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.leader.hash(state);
    }
}

impl ProcessGroup {
    pub fn new(leader: pid_t, session: Option<Arc<Session>>) -> Arc<ProcessGroup> {
        let session = session.unwrap_or_else(|| Session::new(leader));
        let process_group = Arc::new(ProcessGroup {
            session: session.clone(),
            leader,
            mutable_state: RwLock::new(ProcessGroupMutableState {
                thread_groups: BTreeMap::new(),
                orphaned: false,
            }),
        });
        session.write().insert(&process_group);
        process_group
    }

    state_accessor!(ProcessGroup, mutable_state);

    pub fn insert(self: &Arc<Self>, thread_group: &Arc<ThreadGroup>) {
        self.write().thread_groups.insert(thread_group.leader, Arc::downgrade(thread_group));
    }

    /// Removes the thread group from the process group. Returns whether the process group is empty.
    pub fn remove(self: &Arc<Self>, thread_group: &ThreadGroup) -> bool {
        self.write().remove(thread_group)
    }

    pub fn send_signals(self: &Arc<Self>, signals: &[Signal]) {
        let thread_groups = self.read().thread_groups().collect::<Vec<_>>();
        Self::send_signals_to_thread_groups(signals, thread_groups);
    }

    /// Check whether the process group became orphaned. If this is the case, send signals to its
    /// members if at least one is stopped.
    pub fn check_orphaned(self: &Arc<Self>) {
        let thread_groups = {
            let state = self.read();
            if state.orphaned {
                return;
            }
            state.thread_groups().collect::<Vec<_>>()
        };
        for tg in thread_groups {
            let parent = tg.read().parent.clone();
            match parent {
                None => return,
                Some(parent) => {
                    let parent_state = parent.read();
                    if &parent_state.process_group != self
                        && parent_state.process_group.session == self.session
                    {
                        return;
                    }
                }
            }
        }
        let thread_groups = {
            let mut state = self.write();
            if state.orphaned {
                return;
            }
            state.orphaned = true;
            state.thread_groups().collect::<Vec<_>>()
        };
        if thread_groups.iter().any(|tg| tg.read().stopped) {
            Self::send_signals_to_thread_groups(&[SIGHUP, SIGCONT], thread_groups);
        }
    }

    fn send_signals_to_thread_groups(signals: &[Signal], thread_groups: Vec<Arc<ThreadGroup>>) {
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

    /// Removes the thread group from the process group. Returns whether the process group is empty.
    fn remove(&mut self, thread_group: &ThreadGroup) -> bool {
        self.thread_groups.remove(&thread_group.leader);

        self.thread_groups.is_empty()
    }
});
