// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::session_proxy::SessionRegistration;
use std::collections::VecDeque;

/// Tracks which session most recently reported an active status.
#[derive(Default)]
pub struct ActiveSessionQueue {
    active_sessions: VecDeque<SessionRegistration>,
}

impl ActiveSessionQueue {
    /// Returns session which most recently reported an active status if it
    /// exists.
    pub fn active_session(&self) -> Option<SessionRegistration> {
        self.active_sessions.front().cloned()
    }

    /// Promotes a session to the front of the queue and returns whether
    /// the front of the queue was changed.
    pub fn promote_session(&mut self, session: SessionRegistration) -> bool {
        if self.active_session().map(|sr| sr.koid) == Some(session.koid) {
            return false;
        }

        self.remove_session(&session);
        self.active_sessions.push_front(session);
        return true;
    }

    /// Removes a session from the queue and returns whether the front of the
    /// queue was changed.
    pub fn remove_session(&mut self, session: &SessionRegistration) -> bool {
        if self.active_session().map(|sr| sr.koid) == Some(session.koid) {
            self.active_sessions.pop_front();
            true
        } else {
            if let Some(i) = self.active_sessions.iter().position(|sr| sr.koid == session.koid) {
                self.active_sessions.remove(i);
            }
            false
        }
    }
}
