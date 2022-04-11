// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::RwLock;
use std::collections::HashSet;
use std::sync::Arc;

use crate::device::terminal::*;
use crate::types::*;

#[derive(Debug)]
pub struct Session {
    /// The leader of the session
    pub leader: pid_t,

    /// The process groups in the session
    pub process_groups: RwLock<HashSet<pid_t>>,

    /// The controlling terminal of the session.
    pub controlling_terminal: RwLock<Option<ControllingTerminal>>,
}

impl PartialEq for Session {
    fn eq(&self, other: &Self) -> bool {
        self.leader == other.leader
    }
}

impl Session {
    pub fn new(leader: pid_t) -> Arc<Session> {
        let mut process_groups = HashSet::new();
        process_groups.insert(leader);

        Arc::new(Session {
            leader,
            process_groups: RwLock::new(process_groups),
            controlling_terminal: RwLock::new(None),
        })
    }

    /// Removes the process group from the session. Returns whether the session is empty.
    pub fn remove(&self, pid: pid_t) -> bool {
        let mut process_groups = self.process_groups.write();
        process_groups.remove(&pid);
        return process_groups.is_empty();
    }
}

/// The controlling terminal of a session.
#[derive(Debug)]
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
