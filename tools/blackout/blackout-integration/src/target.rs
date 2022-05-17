// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    async_trait::async_trait,
    blackout_target::{Test, TestServer},
};

#[derive(Copy, Clone)]
struct IntegrationTest;

#[async_trait]
impl Test for IntegrationTest {
    async fn setup(&self, _block_device: String, _seed: u64) -> Result<()> {
        log::info!("setup called");
        Ok(())
    }

    async fn test(&self, _block_device: String, _seed: u64) -> Result<()> {
        log::info!("test called");
        loop {}
    }

    async fn verify(&self, block_device: String, _seed: u64) -> Result<()> {
        log::info!("verify called with {}", block_device);

        // We use the block device path to pass an indicator to fail verification, to test the
        // error propagation.
        if block_device == "fail" {
            Err(anyhow::anyhow!("verification failure"))
        } else {
            Ok(())
        }
    }
}

#[fuchsia::main]
async fn main() -> Result<()> {
    let server = TestServer::new(IntegrationTest)?;
    server.serve().await;

    Ok(())
}
