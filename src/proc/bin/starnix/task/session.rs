// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::RwLock;
use std::collections::HashSet;

use crate::types::*;

pub struct Session {
    /// The leader of the session
    pub leader: pid_t,

    /// The process groups in the session
    pub process_groups: RwLock<HashSet<pid_t>>,
}

impl PartialEq for Session {
    fn eq(&self, other: &Self) -> bool {
        self.leader == other.leader
    }
}

impl Session {
    pub fn new(leader: pid_t) -> Session {
        let mut process_groups = HashSet::new();
        process_groups.insert(leader);

        Session { leader, process_groups: RwLock::new(process_groups) }
    }

    /// Removes the process group from the session. Returns whether the session is empty.
    pub fn remove(&mut self, pid: pid_t) -> bool {
        let mut process_groups = self.process_groups.write();
        process_groups.remove(&pid);
        return process_groups.is_empty();
    }
}
