// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod stressor;

use {
    crate::stressor::Stressor,
    anyhow::Result,
    futures::{future::BoxFuture, FutureExt},
    moniker::{ChildMonikerBase, RelativeMoniker, RelativeMonikerBase},
    rand::prelude::SliceRandom,
    rand::rngs::SmallRng,
    rand::Rng,
    stress_test_actor::{actor_loop, Action},
    tracing::warn,
};

const COLLECTION_NAME: &'static str = "dynamic_children";
const ECHO_CLIENT_URL: &'static str = "#meta/unreliable_echo_client.cm";
const NO_BINARY_URL: &'static str = "#meta/no_binary.cm";

#[fuchsia::main]
pub async fn main() -> Result<()> {
    let stressor = Stressor::from_namespace();

    actor_loop(
        stressor,
        vec![
            Action { name: "create_child", run: create_child },
            Action { name: "destroy_child", run: destroy_child },
        ],
    )
    .await
}

pub fn create_child<'a>(
    stressor: &'a mut Stressor,
    mut rng: SmallRng,
) -> BoxFuture<'a, Result<()>> {
    async move {
        let instances = stressor.get_instances_in_realm().await;

        // The root must always be a choice
        let parent_moniker = instances.choose(&mut rng).unwrap();
        let child_name = format!("C{}", rng.gen::<u64>());
        let url = if rng.gen_bool(0.5) { ECHO_CLIENT_URL } else { NO_BINARY_URL };
        let url = url.to_string();

        let result = stressor
            .create_child(parent_moniker, COLLECTION_NAME.to_string(), child_name, url)
            .await;
        if let Err(e) = result {
            // Errors from creation are assumed to be because of collisions with another actor.
            warn!("Ignoring error in create_child operation: {:?}", e)
        }
        Ok(())
    }
    .boxed()
}

pub fn destroy_child<'a>(
    stressor: &'a mut Stressor,
    mut rng: SmallRng,
) -> BoxFuture<'a, Result<()>> {
    async move {
        let instances = stressor.get_instances_in_realm().await;

        // The root cannot be destroyed. Remove it.
        let instances: Vec<String> = instances.into_iter().filter(|m| m != ".").collect();

        if let Some(moniker) = instances.choose(&mut rng) {
            let mut moniker = RelativeMoniker::parse(moniker).unwrap();
            let child_moniker = moniker.down_path_mut().pop().unwrap();
            let child_name = child_moniker.name().to_string();
            let parent_moniker = moniker.to_string();

            let result = stressor
                .destroy_child(&parent_moniker, COLLECTION_NAME.to_string(), child_name)
                .await;
            if let Err(e) = result {
                // Errors from creation are assumed to be because of collisions with another actor.
                warn!("Ignoring error in destroy_child operation: {:?}", e)
            }
        }
        Ok(())
    }
    .boxed()
}
