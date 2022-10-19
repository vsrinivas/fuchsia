// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    async_trait::async_trait,
    blackout_target::{Test, TestServer},
    std::sync::Arc,
};

#[derive(Copy, Clone)]
struct IntegrationTest;

#[async_trait]
impl Test for IntegrationTest {
    async fn setup(
        self: Arc<Self>,
        _device_label: String,
        _device_path: Option<String>,
        _seed: u64,
    ) -> Result<()> {
        tracing::info!("setup called");

        // Make sure we have access to /dev
        let proxy =
            fuchsia_fs::directory::open_in_namespace("/dev", fuchsia_fs::OpenFlags::RIGHT_READABLE)
                .expect("failed to open /dev");
        let _: Vec<_> = proxy.query().await?;

        Ok(())
    }

    async fn test(
        self: Arc<Self>,
        device_label: String,
        device_path: Option<String>,
        _seed: u64,
    ) -> Result<()> {
        tracing::info!("test called");

        // We use the block device path to pass an indicator to loop the test forever, to test
        // returning after the specified duration.
        if device_label == "loop" {
            assert_eq!(device_label, device_path.unwrap());
            futures::future::pending::<()>().await;
        }

        Ok(())
    }

    async fn verify(
        self: Arc<Self>,
        device_label: String,
        device_path: Option<String>,
        _seed: u64,
    ) -> Result<()> {
        tracing::info!("verify called with {}", device_label);

        // We use the block device path to pass an indicator to fail verification, to test the
        // error propagation.
        if device_label == "fail" {
            assert_eq!(device_label, device_path.unwrap());
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
