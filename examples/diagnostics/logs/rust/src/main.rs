// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::component]
async fn main() -> Result<(), anyhow::Error> {
    tracing::trace!("should not print");
    tracing::info!("should print");
    tracing::info!({ foo = 1, bar = "baz" }, "hello, world!");
    log::warn!("warning: using old api");
    Ok(())
}
