// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::test::*, anyhow::*, ffx_core::ffx_plugin, ffx_selftest_args::SelftestCommand,
    std::process::Stdio, std::time::Duration,
};

mod test;

#[ffx_plugin()]
pub async fn selftest(cmd: SelftestCommand) -> Result<()> {
    let default_tests = tests![
        test_isolated,
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

    let out = isolate.ffx(&["config", "get", "test.is-isolated"]).output()?;
    assert_eq!(String::from_utf8(out.stdout)?, "true\n");

    Ok(())
}

async fn test_daemon_echo() -> Result<()> {
    let isolate = Isolate::new("daemon-echo")?;
    let out = isolate.ffx(&["daemon", "echo"]).output().context("failed to execute")?;

    let got = String::from_utf8(out.stdout)?;
    let want = "SUCCESS: received \"Ffx\"\n";
    assert_eq!(got, want);

    Ok(())
}

async fn test_daemon_config_flag() -> Result<()> {
    let isolate = Isolate::new("daemon-config-flag")?;
    let mut daemon =
        isolate.ffx(&["daemon", "start"]).stdout(Stdio::null()).stderr(Stdio::null()).spawn()?;

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
        .output()?;

    assert_eq!(None, daemon.try_wait()?);

    let _out = isolate.ffx(&["daemon", "stop"]).output()?;
    daemon.wait()?;

    Ok(())
}

async fn test_daemon_stop() -> Result<()> {
    let isolate = Isolate::new("daemon-stop")?;
    let out = isolate.ffx(&["daemon", "stop"]).output().context("failed to execute")?;
    let got = String::from_utf8(out.stdout)?;
    let want = "Stopped daemon.\n";

    assert_eq!(got, want);

    Ok(())
}

async fn test_target_list() -> Result<()> {
    let isolate = Isolate::new("target-list")?;

    let mut lines = Vec::<String>::new();

    // It takes a few moments to discover devices on the local network over
    // mdns, so we retry until timeout or a useful value.
    while lines.len() < 2 {
        let mut cmd = isolate.ffx(&["target", "list"]);
        cmd.stderr(Stdio::inherit());
        // Use blocking so that if we get stuck waiting on the subcommand, we
        // don't block the test case timeout.
        // TODO(fxbug.dev/60680): cover this issue in all cases by replacing
        // ffx() return type with a value that handles these semantics.
        let stdout: Result<String> = fuchsia_async::unblock(move || {
            let out = cmd.output().context("failed to execute")?;
            String::from_utf8(out.stdout).context("convert from utf8")
        })
        .await;
        lines = stdout?.lines().map(|s| s.to_owned()).collect();
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

    let mut cmd = isolate.ffx(&["--target", "noexist", "target", "get-ssh-address", "-t", "1"]);
    let (stdout, stderr) = fuchsia_async::unblock(move || {
        let out = cmd.output().context("failed to execute")?;
        let stdout = String::from_utf8(out.stdout).context("convert from utf8")?;
        let stderr = String::from_utf8(out.stderr).context("convert from utf8")?;
        Ok::<_, anyhow::Error>((stdout, stderr))
    })
    .await?;

    ensure!(stdout.lines().count() == 0);
    // stderr names the target, and says timeout.
    ensure!(stderr.contains("noexist"));
    ensure!(stderr.contains("Timeout"));

    Ok(())
}

async fn test_target_get_ssh_address_target_includes_port() -> Result<()> {
    let target_nodename = get_target_nodename().await?;

    let isolate = Isolate::new("get-ssh-address")?;

    let mut cmd =
        isolate.ffx(&["--target", &target_nodename, "target", "get-ssh-address", "-t", "5"]);
    let out = fuchsia_async::unblock(move || {
        let out = cmd.output().context("failed to execute")?;
        Ok::<_, anyhow::Error>(out)
    })
    .await?;

    let stdout = String::from_utf8(out.stdout.clone()).context("convert from utf8")?;
    let stderr = String::from_utf8(out.stderr.clone()).context("convert from utf8")?;

    ensure!(stdout.contains(":22"), "expected stdout to contain ':22', got {:?}", out);
    ensure!(stderr.lines().count() == 0);
    // TODO: establish a good way to assert against the whole target address.

    Ok(())
}

async fn test_manual_target_add_get_ssh_address() -> Result<()> {
    let isolate = Isolate::new("target-add-get-ssh-address")?;

    let mut cmd = isolate.ffx(&["target", "add", "[::1]:8022"]);
    let _ = fuchsia_async::unblock(move || cmd.output()).await;

    let mut cmd = isolate.ffx(&["--target", "[::1]:8022", "target", "get-ssh-address"]);
    let (stdout, stderr) = fuchsia_async::unblock(move || {
        let out = cmd.output().context("failed to execute")?;
        let stdout = String::from_utf8(out.stdout).context("convert from utf8")?;
        let stderr = String::from_utf8(out.stderr).context("convert from utf8")?;
        Ok::<_, anyhow::Error>((stdout, stderr))
    })
    .await?;

    ensure!(stdout.contains("[::1]:8022"));
    ensure!(stderr.lines().count() == 0);
    // TODO: establish a good way to assert against the whole target address.

    Ok(())
}

async fn test_manual_target_add_get_ssh_address_late_add() -> Result<()> {
    let isolate = Isolate::new("target-add-get-ssh-address")?;

    let mut cmd = isolate.ffx(&["--target", "[::1]:8022", "target", "get-ssh-address", "-t", "10"]);
    let task = fuchsia_async::unblock(move || {
        let out = cmd.output().context("failed to execute")?;
        let stdout = String::from_utf8(out.stdout).context("convert from utf8")?;
        let stderr = String::from_utf8(out.stderr).context("convert from utf8")?;
        Ok::<_, anyhow::Error>((stdout, stderr))
    });

    // The get-ssh-address should pick up targets added after it has started, as well as before.
    fuchsia_async::Timer::new(Duration::from_millis(500)).await;

    let mut cmd = isolate.ffx(&["target", "add", "[::1]:8022"]);
    let _ = fuchsia_async::unblock(move || cmd.output()).await;

    let (stdout, stderr) = task.await?;

    ensure!(stdout.contains("[::1]:8022"));
    ensure!(stderr.lines().count() == 0);
    // TODO: establish a good way to assert against the whole target address.

    Ok(())
}
