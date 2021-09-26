// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This actor has one action that fails immediately.

use {
    anyhow::{anyhow, Result},
    futures::{future::BoxFuture, FutureExt},
    rand::rngs::SmallRng,
    stress_test_actor::{actor_loop, Action},
};

#[fuchsia::component(logging = false)]
pub async fn main() -> Result<()> {
    // TODO(84952): This syntax is complex and can be simplified using Rust macros.
    actor_loop((), vec![Action { name: "fails_immediately", run: fails_immediately }]).await
}

pub fn fails_immediately<'a>(_: &'a mut (), _: SmallRng) -> BoxFuture<'a, Result<()>> {
    async move { Err(anyhow!("This action is expected to fail immediately")) }.boxed()
}
