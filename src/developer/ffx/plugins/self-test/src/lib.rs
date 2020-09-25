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
    let default_tests = tests![test_daemon_echo, test_daemon_stop,];
    let mut target_tests = tests![test_target_list,];

    let mut tests = default_tests;
    if cmd.include_target {
        tests.append(&mut target_tests);
    }

    run(tests, Duration::from_secs(cmd.timeout), Duration::from_secs(cmd.case_timeout)).await
}

async fn test_daemon_echo() -> Result<()> {
    let out = ffx(&["daemon", "echo"]).output().context("failed to execute")?;

    let got = String::from_utf8(out.stdout)?;
    let want = "SUCCESS: received \"Ffx\"\n";
    assert_eq!(got, want);

    Ok(())
}

async fn test_daemon_stop() -> Result<()> {
    let out = ffx(&["daemon", "stop"]).output().context("failed to execute")?;
    let got = String::from_utf8(out.stdout)?;
    let want = "Stopped daemon.\n";

    assert_eq!(got, want);

    Ok(())
}

async fn test_target_list() -> Result<()> {
    let mut lines = Vec::<String>::new();

    // It takes a few moments to discover devices on the local network over
    // mdns, so we retry until timeout or a useful value.
    while lines.len() < 2 {
        // Use blocking so that if we get stuck waiting on the subcommand, we
        // don't block the test case timeout.
        // TODO(fxbug.dev/60680): cover this issue in all cases by replacing
        // ffx() return type with a value that handles these semantics.
        let stdout: Result<String> = fuchsia_async::Task::blocking(async {
            let mut cmd = ffx(&["target", "list"]);
            cmd.stderr(Stdio::inherit());
            let out = cmd.output().context("failed to execute")?;
            String::from_utf8(out.stdout).context("convert from utf8")
        })
        .await;
        lines = stdout?.lines().map(|s| s.to_owned()).collect();
    }

    ensure!(lines.len() >= 2, format!("expected more than one line of output, got:\n{:?}", lines));

    let headers = vec!["NAME", "TYPE", "STATE", "ADDRS/IP", "AGE", "RCS"];
    let headerline = &lines[0];
    for (got, want) in headerline.split_whitespace().zip(headers) {
        ensure!(got == want, format!("assertion failed:\nLEFT: {:?}\nRIGHT: {:?}", got, want));
    }
    Ok(())
}
