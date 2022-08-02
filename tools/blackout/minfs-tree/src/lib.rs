// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, blackout_host::TestEnv, ffx_core::ffx_plugin,
    ffx_storage_blackout_minfs_tree_args::MinfsTreeCommand, std::time::Duration,
};

#[ffx_plugin("storage_dev")]
pub async fn minfs_tree(cmd: MinfsTreeCommand) -> Result<()> {
    let opts = blackout_host::CommonOpts {
        block_device: cmd.block_device,
        seed: cmd.seed,
        reboot: if cmd.dmc_reboot {
            blackout_host::RebootType::Dmc
        } else if cmd.relay.is_some() {
            blackout_host::RebootType::Hardware(cmd.relay.unwrap())
        } else {
            blackout_host::RebootType::Software
        },
        iterations: cmd.iterations,
        run_until_failure: cmd.run_until_failure,
    };
    let mut test =
        TestEnv::new("blackout-minfs-tree-target", "blackout-minfs-tree-target-component", opts)
            .await;
    test.setup_step()
        .load_step(Duration::from_secs(30))
        .reboot_step(false)
        .verify_step(20, Duration::from_secs(10));
    test.run().await?;
    Ok(())
}
