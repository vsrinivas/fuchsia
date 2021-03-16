// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::component::Component,
    crate::tree_actor::TreeActor,
    async_trait::async_trait,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::connect_to_service,
    futures::lock::Mutex,
    rand::{rngs::SmallRng, SeedableRng},
    std::sync::Arc,
    stress_test::{actor::ActorRunner, environment::Environment, random_seed},
};

#[derive(Debug)]
pub struct TreeStressorEnvironment {
    test_realm_svc: fsys::RealmProxy,
    tree_actor: Arc<Mutex<TreeActor>>,
    time_limit_secs: Option<u64>,
    num_operations: Option<u64>,
    seed: u128,
}

impl TreeStressorEnvironment {
    pub async fn new(
        time_limit_secs: Option<u64>,
        num_operations: Option<u64>,
        component_limit: usize,
    ) -> Self {
        let test_realm_svc = connect_to_service::<fsys::RealmMarker>()
            .expect("Could not connect to Realm service in test namespace");

        let tree_root = Component::new(&test_realm_svc, "test_tree_root").await;

        let seed = random_seed();
        let rng = SmallRng::from_seed(seed.to_le_bytes());
        let tree_actor = Arc::new(Mutex::new(TreeActor::new(tree_root, rng, component_limit)));

        Self { num_operations, time_limit_secs, test_realm_svc, tree_actor, seed }
    }
}

#[async_trait]
impl Environment for TreeStressorEnvironment {
    fn target_operations(&self) -> Option<u64> {
        self.num_operations
    }

    fn timeout_seconds(&self) -> Option<u64> {
        self.time_limit_secs
    }

    fn actor_runners(&mut self) -> Vec<ActorRunner> {
        vec![ActorRunner::new("tree_actor", 0, self.tree_actor.clone())]
    }

    async fn reset(&mut self) {
        unreachable!("This stress test will never reset")
    }
}
