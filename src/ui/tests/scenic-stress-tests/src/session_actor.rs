// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::session::Session,
    async_trait::async_trait,
    rand::{rngs::SmallRng, Rng},
    stress_test::actor::{Actor, ActorError},
};

pub struct SessionActor {
    root_session: Session,
    rng: SmallRng,
}

impl SessionActor {
    pub fn new(rng: SmallRng, root_session: Session) -> Self {
        Self { rng, root_session }
    }
}

#[async_trait]
impl Actor for SessionActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        let mut cur_session = &mut self.root_session;
        loop {
            if cur_session.has_children() && self.rng.gen_bool(0.8) {
                // Traverse further
                cur_session = cur_session.get_random_child_mut(&mut self.rng);
            } else if cur_session.has_children() && self.rng.gen_bool(0.5) {
                // Delete a random session
                cur_session.delete_child(&mut self.rng);
                break;
            } else {
                // Create a new session
                cur_session.add_child(&mut self.rng);
                break;
            }
        }
        Ok(())
    }
}
