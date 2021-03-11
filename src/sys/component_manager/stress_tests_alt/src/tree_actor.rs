use rand::Rng;

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::component::Component,
    async_trait::async_trait,
    log::debug,
    rand::rngs::SmallRng,
    stress_test::actor::{Actor, ActorError},
};

pub struct TreeActor {
    tree_root: Component,
    rng: SmallRng,
}

impl TreeActor {
    pub fn new(tree_root: Component, rng: SmallRng) -> Self {
        Self { tree_root, rng }
    }
}

#[async_trait]
impl Actor for TreeActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        // The actor is heavily biased against destroying components.
        if self.tree_root.has_children() && self.rng.gen_bool(0.2) {
            debug!("Destroying random child");
            self.tree_root.traverse_and_destroy_random_child(&mut self.rng).await;
        } else {
            debug!("Adding random child");
            self.tree_root.traverse_and_add_random_child(&mut self.rng).await;
        }
        Ok(())
    }
}
