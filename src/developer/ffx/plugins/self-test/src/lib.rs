// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::test::*, anyhow::*, ffx_core::ffx_plugin, ffx_selftest_args::SelftestCommand,
    std::time::Duration,
};

mod test;

#[ffx_plugin()]
pub async fn selftest(cmd: SelftestCommand) -> Result<()> {
    let default_tests = tests![
        test_isolated,
        test_experiment_not_enabled,
        test_experiment_enabled,
        test_daemon_echo,
        test_daemon_config_flag,
        test_daemon_stop,
        test_target_get_ssh_address_timeout,
        test_manual_target_add_get_ssh_address,
        test_manual_target_add_get_ssh_address_late_add,
    ];
    let mut target_tests =
        tests![test_target_list, test_target_get_ssh_address_target_includes_port];

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

async fn test_experiment_not_enabled() -> Result<()> {
    let isolate = Isolate::new("experiment-not-enabled")?;

    let out = isolate.ffx(&["self-test", "experiment"]).await?;

    ensure!(out.stdout.lines().count() == 0, "stdout unexpectedly contains output: {:?}", out);
    ensure!(!out.status.success());
    ensure!(out.stderr.contains("experimental subcommand"), "stderr is unexpected: {:?}", out);
    ensure!(out.stderr.contains("selftest.experiment"), "stderr is unexpected: {:?}", out);

    Ok(())
}

async fn test_experiment_enabled() -> Result<()> {
    let isolate = Isolate::new("experiment-enabled")?;

    let _ = isolate.ffx(&["config", "set", "selftest.experiment", "true"]).await?;

    let out = isolate.ffx(&["self-test", "experiment"]).await?;

    ensure!(out.stderr.lines().count() == 0, "stderr is unexpected: {:?}", out);
    ensure!(out.status.success());

    Ok(())
}

async fn test_daemon_echo() -> Result<()> {
    let isolate = Isolate::new("daemon-echo")?;
    let out = isolate.ffx(&["daemon", "echo"]).await?;

    let want = "SUCCESS: received \"Ffx\"\n";
    assert_eq!(out.stdout, want);

    Ok(())
}

async fn test_daemon_config_flag() -> Result<()> {
    let isolate = Isolate::new("daemon-config-flag")?;
    let mut daemon = isolate.ffx_spawn(&["daemon", "start"])?;

    // This should not terminate the daemon just started, as it won't
    // share an overnet socket with it.
    let mut ascendd_path2 = isolate.ascendd_path.clone();
    ascendd_path2.set_extension("2");
    let _out = isolate
        .ffx(&[
            "--config",
            &format!("overnet.socket={}", ascendd_path2.to_string_lossy()),
            "daemon",
            "stop",
        ])
        .await?;

    assert_eq!(None, daemon.try_wait()?);

    let _out = isolate.ffx(&["daemon", "stop"]).await?;
    daemon.wait()?;

    Ok(())
}

async fn test_daemon_stop() -> Result<()> {
    let isolate = Isolate::new("daemon-stop")?;
    let out = isolate.ffx(&["daemon", "stop"]).await?;
    let want = "Stopped daemon.\n";

    assert_eq!(out.stdout, want);

    Ok(())
}

async fn test_target_list() -> Result<()> {
    let isolate = Isolate::new("target-list")?;

    let mut lines = Vec::<String>::new();

    // It takes a few moments to discover devices on the local network over
    // mdns, so we retry until timeout or a useful value.
    while lines.len() < 2 {
        let out = isolate.ffx(&["target", "list"]).await?;
        // cmd.stderr(Stdio::inherit());
        lines = out.stdout.lines().map(|s| s.to_owned()).collect();
    }

    ensure!(lines.len() >= 2, format!("expected more than one line of output, got:\n{:?}", lines));

    let headers = vec!["NAME", "SERIAL", "TYPE", "STATE", "ADDRS/IP", "RCS"];
    let headerline = &lines[0];
    for (got, want) in headerline.split_whitespace().zip(headers) {
        ensure!(got == want, format!("assertion failed:\nLEFT: {:?}\nRIGHT: {:?}", got, want));
    }
    Ok(())
}

async fn test_target_get_ssh_address_timeout() -> Result<()> {
    let isolate = Isolate::new("get-ssh-address")?;

    let out = isolate.ffx(&["--target", "noexist", "target", "get-ssh-address", "-t", "1"]).await?;

    ensure!(out.stdout.lines().count() == 0, "stdout is unexpected: {:?}", out);
    // stderr names the target, and says timeout.
    ensure!(out.stderr.contains("noexist"), "stderr is unexpected: {:?}", out);
    ensure!(out.stderr.contains("Timeout"), "stderr is unexpected: {:?}", out);

    Ok(())
}

async fn test_target_get_ssh_address_target_includes_port() -> Result<()> {
    let target_nodename = get_target_nodename().await?;

    let isolate = Isolate::new("get-ssh-address")?;

    let out = isolate
        .ffx(&["--target", &target_nodename, "target", "get-ssh-address", "-t", "5"])
        .await?;

    ensure!(out.stdout.contains(":22"), "expected stdout to contain ':22', got {:?}", out);
    ensure!(out.stderr.lines().count() == 0, "stderr is unexpected: {:?}", out);
    // TODO: establish a good way to assert against the whole target address.

    Ok(())
}

async fn test_manual_target_add_get_ssh_address() -> Result<()> {
    let isolate = Isolate::new("target-add-get-ssh-address")?;

    let _ = isolate.ffx(&["target", "add", "[::1]:8022"]).await?;

    let out = isolate.ffx(&["--target", "[::1]:8022", "target", "get-ssh-address"]).await?;

    ensure!(out.stdout.contains("[::1]:8022"), "stdout is unexpected: {:?}", out);
    ensure!(out.stderr.lines().count() == 0, "stderr is unexpected: {:?}", out);
    // TODO: establish a good way to assert against the whole target address.

    Ok(())
}

async fn test_manual_target_add_get_ssh_address_late_add() -> Result<()> {
    let isolate = Isolate::new("target-add-get-ssh-address-late-add")?;

    let task = isolate.ffx(&["--target", "[::1]:8022", "target", "get-ssh-address", "-t", "10"]);

    // The get-ssh-address should pick up targets added after it has started, as well as before.
    fuchsia_async::Timer::new(Duration::from_millis(500)).await;

    let _ = isolate.ffx(&["target", "add", "[::1]:8022"]).await?;

    let out = task.await?;

    ensure!(out.stdout.contains("[::1]:8022"), "stdout is unexpected: {:?}", out);
    ensure!(out.stderr.lines().count() == 0, "stderr is unexpected: {:?}", out);
    // TODO: establish a good way to assert against the whole target address.

    Ok(())
}
