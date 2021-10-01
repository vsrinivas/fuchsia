// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod hub;

use {
    crate::hub::Hub,
    anyhow::Result,
    futures::{future::BoxFuture, FutureExt},
    log::warn,
    rand::rngs::SmallRng,
    stress_test_actor::{actor_loop, Action},
};

#[fuchsia::component]
pub async fn main() -> Result<()> {
    // Create a Hub object
    let hub = Hub::from_namespace()?;

    actor_loop(
        hub,
        vec![
            Action { name: "create_child", run: create_child },
            Action { name: "delete_child", run: delete_child },
        ],
    )
    .await
}

pub fn create_child<'a>(hub: &'a mut Hub, rng: SmallRng) -> BoxFuture<'a, Result<()>> {
    async move {
        let result = hub.traverse_and_add(rng).await;
        if let Err(e) = result {
            // Errors from traversal and creation are assumed to be because
            // of collisions with another actor.
            warn!("Ignoring error in create_child operation: {:?}", e)
        }
        Ok(())
    }
    .boxed()
}

pub fn delete_child<'a>(hub: &'a mut Hub, rng: SmallRng) -> BoxFuture<'a, Result<()>> {
    async move {
        let result = hub.traverse_and_delete(rng).await;
        if let Err(e) = result {
            // Errors from traversal and deletion are assumed to be because
            // of collisions with another actor.
            warn!("Ignoring error in delete_child operation: {:?}", e)
        }
        Ok(())
    }
    .boxed()
}
