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
    component_limit: usize,
}

impl TreeActor {
    pub fn new(tree_root: Component, rng: SmallRng, component_limit: usize) -> Self {
        Self { tree_root, rng, component_limit }
    }
}

#[async_trait]
impl Actor for TreeActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        if self.tree_root.num_descendants() >= self.component_limit {
            debug!("Component limit exceeded. Destroying random child");
            self.tree_root.traverse_and_destroy_random_child(&mut self.rng).await;
        } else {
            debug!("Adding random child");
            self.tree_root.traverse_and_add_random_child(&mut self.rng).await;
        }
        Ok(())
    }
}
