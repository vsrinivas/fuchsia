// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::hub::Hub,
    async_trait::async_trait,
    rand::rngs::SmallRng,
    stress_test::actor::{Actor, ActorError},
};

pub struct TreeActor {
    hub: Hub,
    rng: SmallRng,
    _component_limit: usize,
}

impl TreeActor {
    pub fn new(rng: SmallRng, component_limit: usize) -> Self {
        let hub = Hub::from_namespace().expect("Could not open hub from namespace");
        Self { rng, _component_limit: component_limit, hub }
    }
}

#[async_trait]
impl Actor for TreeActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        // Ignore the errors encountered during traversal, creation and deletion.
        // It is safe to assume that these errors arise from the actions of another actor
        // operating on the same topology concurrently.
        let _ = self.hub.traverse_and_mutate(&mut self.rng).await;
        Ok(())
    }
}
