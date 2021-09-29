// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::test::*, anyhow::*, std::time::Duration};

pub(crate) async fn test_get_ssh_address_timeout() -> Result<()> {
    let isolate = Isolate::new("get-ssh-address")?;

    let out = isolate.ffx(&["--target", "noexist", "target", "get-ssh-address", "-t", "1"]).await?;

    ensure!(out.stdout.lines().count() == 0, "stdout is unexpected: {:?}", out);
    // stderr names the target, and says timeout.
    ensure!(out.stderr.contains("noexist"), "stderr is unexpected: {:?}", out);
    ensure!(out.stderr.contains("Timeout"), "stderr is unexpected: {:?}", out);

    Ok(())
}

pub(crate) async fn test_manual_add_get_ssh_address() -> Result<()> {
    let isolate = Isolate::new("target-add-get-ssh-address")?;

    let _ = isolate.ffx(&["target", "add", "[::1]:8022"]).await?;

    let out = isolate.ffx(&["--target", "[::1]:8022", "target", "get-ssh-address"]).await?;

    ensure!(out.stdout.contains("[::1]:8022"), "stdout is unexpected: {:?}", out);
    ensure!(out.stderr.lines().count() == 0, "stderr is unexpected: {:?}", out);
    // TODO: establish a good way to assert against the whole target address.

    Ok(())
}

pub(crate) async fn test_manual_add_get_ssh_address_late_add() -> Result<()> {
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

pub mod include_target {
    use super::*;

    pub(crate) async fn test_list() -> Result<()> {
        let isolate = Isolate::new("target-list")?;

        let mut lines = Vec::<String>::new();

        // It takes a few moments to discover devices on the local network over
        // mdns, so we retry until timeout or a useful value.
        while lines.len() < 2 {
            let out = isolate.ffx(&["target", "list"]).await?;
            // cmd.stderr(Stdio::inherit());
            lines = out.stdout.lines().map(|s| s.to_owned()).collect();
        }

        ensure!(
            lines.len() >= 2,
            format!("expected more than one line of output, got:\n{:?}", lines)
        );

        let headers = vec!["NAME", "SERIAL", "TYPE", "STATE", "ADDRS/IP", "RCS"];
        let headerline = &lines[0];
        for (got, want) in headerline.split_whitespace().zip(headers) {
            ensure!(got == want, format!("assertion failed:\nLEFT: {:?}\nRIGHT: {:?}", got, want));
        }
        Ok(())
    }

    pub(crate) async fn test_get_ssh_address_includes_port() -> Result<()> {
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
}
