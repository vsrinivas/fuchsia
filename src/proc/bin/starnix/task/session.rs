// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::BTreeMap;
use std::sync::{Arc, Weak};

use crate::device::terminal::*;
use crate::lock::RwLock;
use crate::mutable_state::*;
use crate::task::*;
use crate::types::*;

#[derive(Debug)]
pub struct SessionMutableState {
    /// The process groups in the session
    ///
    /// The references to ProcessGroup is weak to prevent cycles as ProcessGroup have a Arc reference to their
    /// session.
    /// It is still expected that these weak references are always valid, as process groups must unregister
    /// themselves before they are deleted.
    process_groups: BTreeMap<pid_t, Weak<ProcessGroup>>,

    /// The controlling terminal of the session.
    pub controlling_terminal: Option<ControllingTerminal>,
}

#[derive(Debug)]
pub struct Session {
    /// The leader of the session
    pub leader: pid_t,

    /// The mutable state of the Session.
    mutable_state: RwLock<SessionMutableState>,
}

impl PartialEq for Session {
    fn eq(&self, other: &Self) -> bool {
        self.leader == other.leader
    }
}

impl Session {
    pub fn new(leader: pid_t) -> Arc<Session> {
        Arc::new(Session {
            leader,
            mutable_state: RwLock::new(SessionMutableState {
                process_groups: BTreeMap::new(),
                controlling_terminal: None,
            }),
        })
    }

    state_accessor!(Session, mutable_state);
}

#[apply(state_implementation!)]
impl SessionMutableState<Base = Session> {
    /// Removes the process group from the session. Returns whether the session is empty.
    pub fn remove(&mut self, pid: pid_t) {
        self.process_groups.remove(&pid);
    }

    pub fn insert(&mut self, process_group: &Arc<ProcessGroup>) {
        self.process_groups.insert(process_group.leader, Arc::downgrade(process_group));
    }
}

/// The controlling terminal of a session.
#[derive(Clone, Debug)]
pub struct ControllingTerminal {
    /// The controlling terminal.
    pub terminal: Arc<Terminal>,
    /// Whether the session is associated to the main or replica side of the terminal.
    pub is_main: bool,
}

impl ControllingTerminal {
    pub fn new(terminal: Arc<Terminal>, is_main: bool) -> Self {
        Self { terminal, is_main }
    }
}
