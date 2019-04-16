// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::session_proxy::{Session, SessionRegistration};
use fuchsia_zircon as zx;
use std::collections::HashMap;

/// `SessionList` holds a set of sessions associated with their registrations.
#[derive(Default)]
pub struct SessionList {
    sessions: HashMap<zx::Koid, (SessionRegistration, Session)>,
}

impl SessionList {
    pub fn list(&self) -> impl Iterator<Item = &SessionRegistration> {
        self.sessions.values().map(|(r, _)| r)
    }

    pub fn get(&self, koid: zx::Koid) -> Option<&Session> {
        self.sessions.get(&koid).map(|(_, s)| s)
    }

    pub fn push(&mut self, registration: SessionRegistration, session: Session) {
        self.sessions.insert(registration.koid, (registration, session));
    }

    pub fn remove(&mut self, koid: zx::Koid) {
        self.sessions.remove(&koid);
    }
}
