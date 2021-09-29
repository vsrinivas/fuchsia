// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::test::*, anyhow::*, ffx_core::ffx_plugin, ffx_selftest_args::SelftestCommand,
    std::time::Duration,
};

mod daemon;
mod experiment;
mod target;
mod test;

#[ffx_plugin()]
pub async fn selftest(cmd: SelftestCommand) -> Result<()> {
    let default_tests = tests![
        test_isolated,
        experiment::test_not_enabled,
        experiment::test_enabled,
        daemon::test_echo,
        daemon::test_config_flag,
        daemon::test_stop,
        target::test_get_ssh_address_timeout,
        target::test_manual_add_get_ssh_address,
        target::test_manual_add_get_ssh_address_late_add,
    ];

    let mut target_tests = tests![
        target::include_target::test_list,
        target::include_target::test_get_ssh_address_includes_port
    ];

    let mut tests = default_tests;
    if cmd.include_target {
        tests.append(&mut target_tests);
    }

    run(tests, Duration::from_secs(cmd.timeout), Duration::from_secs(cmd.case_timeout)).await
}

async fn test_isolated() -> Result<()> {
    let isolate = Isolate::new("isolated")?;

    let out = isolate.ffx(&["config", "get", "test.is-isolated"]).await?;
    assert_eq!(out.stdout, "true\n");

    Ok(())
}
