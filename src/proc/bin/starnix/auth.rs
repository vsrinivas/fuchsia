// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::types::*;

#[derive(Default, Clone)]
pub struct Credentials {
    pub uid: uid_t,
    pub gid: gid_t,
    pub euid: uid_t,
    pub egid: gid_t,
    pub saved_uid: uid_t,
    pub saved_gid: gid_t,
}

impl Credentials {
    /// Creates a new set of credentials with all ids set to `id`.
    pub fn new(id: uid_t) -> Credentials {
        Credentials { uid: id, gid: id, euid: id, egid: id, saved_uid: id, saved_gid: id }
    }

    /// Compares the user ID of `self` to that of `other`.
    ///
    /// Used to check whether a task can signal another.
    ///
    /// From https://man7.org/linux/man-pages/man2/kill.2.html:
    ///
    /// > For a process to have permission to send a signal, it must either be
    /// > privileged (under Linux: have the CAP_KILL capability in the user
    /// > namespace of the target process), or the real or effective user ID of
    /// > the sending process must equal the real or saved set- user-ID of the
    /// > target process.
    ///
    /// Returns true if the credentials are considered to have the same user ID.
    pub fn has_same_uid(&self, other: &Credentials) -> bool {
        self.euid == other.saved_uid
            || self.euid == other.uid
            || self.uid == other.uid
            || self.uid == other.saved_uid
    }
}

/// Represents the IDs used to abstract shell job control.
#[derive(Default, Clone)]
pub struct ShellJobControl {
    /// The process session ID of a task.
    pub sid: pid_t,
    /// The process group ID of a task.
    pub pgrp: pid_t,
}

impl ShellJobControl {
    pub fn new(id: pid_t) -> ShellJobControl {
        ShellJobControl { sid: id, pgrp: id }
    }
}
