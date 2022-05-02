// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, blackout_host::TestEnv, ffx_core::ffx_plugin,
    ffx_storage_blackout_integration_args::BlackoutIntegrationCommand, std::time::Duration,
};

async fn failure() -> Result<()> {
    let opts = blackout_host::CommonOpts {
        block_device: String::from("/nothing"),
        seed: None,
        relay: None,
        iterations: None,
        run_until_failure: false,
    };
    let mut test = TestEnv::new_component(
        "blackout-integration-target",
        "blackout-integration-target-failure-component",
        opts,
    )
    .await;
    test.setup_step().load_step(Duration::from_secs(1)).verify_step(10, Duration::from_secs(0));
    match test.run() {
        Ok(()) => Err(anyhow::anyhow!("got a successful return from a failing test")),
        Err(blackout_host::BlackoutError::Verification(_)) => Ok(()),
        Err(e) => Err(anyhow::anyhow!("got the wrong error from a failing test: {:?}", e)),
    }
}

async fn success() -> Result<()> {
    let opts = blackout_host::CommonOpts {
        block_device: String::from("/nothing"),
        seed: None,
        relay: None,
        iterations: None,
        run_until_failure: false,
    };
    let mut test = TestEnv::new_component(
        "blackout-integration-target",
        "blackout-integration-target-component",
        opts,
    )
    .await;
    test.setup_step().load_step(Duration::from_secs(1)).verify_step(20, Duration::from_secs(15));
    test.run()?;

    Ok(())
}

#[ffx_plugin("storage_dev")]
async fn integration(_cmd: BlackoutIntegrationCommand) -> Result<()> {
    // make sure verification failure detection works
    failure().await?;

    // make sure a successful test run works
    success().await?;

    Ok(())
}
