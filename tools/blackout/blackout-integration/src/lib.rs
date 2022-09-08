// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    blackout_host::{BlackoutError, TestEnv},
    ffx_core::ffx_plugin,
    ffx_storage_blackout_integration_args::BlackoutIntegrationCommand,
    std::time::Duration,
};

async fn failure(reboot: bool, bootserver: bool, dmc_reboot: bool) -> Result<()> {
    let opts = blackout_host::CommonOpts {
        device_label: Some(String::from("fail")),
        device_path: Some(String::from("fail")),
        seed: None,
        reboot: if dmc_reboot {
            blackout_host::RebootType::Dmc
        } else {
            blackout_host::RebootType::Software
        },
        iterations: None,
        run_until_failure: false,
    };
    let mut test =
        TestEnv::new("blackout-integration-target", "blackout-integration-target-component", opts)
            .await;

    test.setup_step().load_step(None);
    if reboot {
        test.reboot_step(bootserver);
    }
    test.verify_step(20, Duration::from_secs(15));

    match test.run().await {
        Err(BlackoutError::Verification(_)) => Ok(()),
        Ok(()) => Err(anyhow::anyhow!("test succeeded when it should've failed")),
        Err(e) => Err(anyhow::anyhow!("test failed, but not in the expected way: {:?}", e)),
    }
}

async fn success(
    iterations: Option<u64>,
    reboot: bool,
    bootserver: bool,
    dmc_reboot: bool,
) -> Result<()> {
    let opts = blackout_host::CommonOpts {
        device_label: Some(String::from("loop")),
        device_path: Some(String::from("loop")),
        seed: None,
        reboot: if dmc_reboot {
            blackout_host::RebootType::Dmc
        } else {
            blackout_host::RebootType::Software
        },
        iterations: iterations,
        run_until_failure: false,
    };
    let mut test =
        TestEnv::new("blackout-integration-target", "blackout-integration-target-component", opts)
            .await;

    test.setup_step().load_step(Some(Duration::from_secs(1)));
    if reboot {
        test.reboot_step(bootserver);
    }
    test.verify_step(20, Duration::from_secs(15));
    test.run().await?;

    Ok(())
}

#[ffx_plugin("storage_dev")]
async fn integration(cmd: BlackoutIntegrationCommand) -> Result<()> {
    // make sure verification failure detection works
    println!("testing a verification failure...");
    failure(cmd.reboot, cmd.bootserver, cmd.dmc_reboot).await?;

    // make sure a successful test run works
    println!("testing a successful run...");
    success(cmd.iterations, cmd.reboot, cmd.bootserver, cmd.dmc_reboot).await?;

    Ok(())
}
