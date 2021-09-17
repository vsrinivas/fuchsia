// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::tree_actor::TreeActor,
    async_trait::async_trait,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::connect_to_protocol,
    futures::lock::Mutex,
    rand::{rngs::SmallRng, Rng, SeedableRng},
    std::sync::Arc,
    stress_test::{actor::ActorRunner, environment::Environment, random_seed},
};

#[derive(Debug)]
pub struct TreeStressorEnvironment {
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    test_realm_svc: fsys::RealmProxy,
    tree_actors: Vec<Arc<Mutex<TreeActor>>>,
    time_limit_secs: Option<u64>,
    num_operations: Option<u64>,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    seed: u128,
}

impl TreeStressorEnvironment {
    pub async fn new(
        time_limit_secs: Option<u64>,
        num_operations: Option<u64>,
        component_limit: usize,
    ) -> Self {
        let test_realm_svc = connect_to_protocol::<fsys::RealmMarker>()
            .expect("Could not connect to Realm service in test namespace");

        let seed = random_seed();
        let mut rng = SmallRng::from_seed(seed.to_le_bytes());

        let mut tree_actors = vec![];

        // Create 10 tree actors that work from the same root component
        for _ in 0..10 {
            let actor_seed = rng.gen::<u128>();
            let actor_rng = SmallRng::from_seed(actor_seed.to_le_bytes());
            let tree_actor = Arc::new(Mutex::new(TreeActor::new(actor_rng, component_limit).await));
            tree_actors.push(tree_actor);
        }

        Self { num_operations, time_limit_secs, test_realm_svc, tree_actors, seed }
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
        let mut runners = vec![];
        for (index, actor) in self.tree_actors.iter().enumerate() {
            let name = format!("tree_actor_{}", index);
            runners.push(ActorRunner::new(name, None, actor.clone()))
        }
        runners
    }

    async fn reset(&mut self) {
        unreachable!("This stress test will never reset")
    }
}
