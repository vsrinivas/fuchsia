// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This actor has one action that never returns.

use {
    anyhow::Result,
    futures::{future::pending, future::BoxFuture, FutureExt},
    rand::rngs::SmallRng,
    stress_test_actor::{actor_loop, Action},
};

#[fuchsia::component(logging = false)]
pub async fn main() -> Result<()> {
    // TODO(84952): This syntax is complex and can be simplified using Rust macros.
    actor_loop((), vec![Action { name: "takes_too_long", run: takes_too_long }]).await
}

pub fn takes_too_long<'a>(_: &'a mut (), _: SmallRng) -> BoxFuture<'a, Result<()>> {
    async move {
        pending::<()>().await;
        Ok(())
    }
    .boxed()
}
