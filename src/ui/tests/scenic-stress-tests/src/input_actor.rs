// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::pointer_state::PointerState,
    async_trait::async_trait,
    fuchsia_scenic as scenic,
    rand::rngs::SmallRng,
    stress_test::actor::{Actor, ActorError},
};

pub struct InputActor {
    session: scenic::SessionPtr,
    state: PointerState,
    rng: SmallRng,
}

impl InputActor {
    pub fn new(rng: SmallRng, session: scenic::SessionPtr, compositor_id: u32) -> Self {
        let state = PointerState::new(compositor_id);
        Self { rng, state, session }
    }
}

#[async_trait]
impl Actor for InputActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        self.state.next_phase();
        let command = self.state.command(&mut self.rng);
        let mut session = self.session.lock();
        session.enqueue(command);
        Ok(())
    }
}
