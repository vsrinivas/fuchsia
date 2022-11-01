// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::flatland_instance::FlatlandInstance,
    async_trait::async_trait,
    fuchsia_component_test::RealmInstance,
    rand::{rngs::SmallRng, Rng},
    std::sync::Arc,
    stress_test::actor::{Actor, ActorError},
};

pub struct FlatlandActor {
    root: FlatlandInstance,
    rng: SmallRng,
    realm: Arc<RealmInstance>,
}

impl FlatlandActor {
    pub fn new(rng: SmallRng, root: FlatlandInstance, realm: Arc<RealmInstance>) -> Self {
        Self { rng, root, realm }
    }
}

// Does a random walk down the tree to select an instance.
// This makes ancestors more likely to be selected.
fn find_random_instance<'a>(
    root: &'a mut FlatlandInstance,
    rng: &mut SmallRng,
) -> &'a mut FlatlandInstance {
    let mut current = root;
    while current.has_children() && rng.gen_bool(0.8) {
        current = current.get_random_child_mut(rng);
    }
    current
}

#[async_trait]
impl Actor for FlatlandActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        let instance = find_random_instance(&mut self.root, &mut self.rng);
        if instance.has_children() && self.rng.gen_bool(0.5) {
            instance.delete_child(&mut self.rng).await;
        } else {
            instance.add_child(&self.realm.root).await;
        }
        Ok(())
    }
}
