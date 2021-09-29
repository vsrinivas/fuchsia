// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{assert_eq, test::*},
    anyhow::*,
};

pub(crate) async fn test_echo() -> Result<()> {
    let isolate = Isolate::new("daemon-echo")?;
    let out = isolate.ffx(&["daemon", "echo"]).await?;

    let want = "SUCCESS: received \"Ffx\"\n";
    assert_eq!(out.stdout, want);

    Ok(())
}

pub(crate) async fn test_config_flag() -> Result<()> {
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

pub(crate) async fn test_stop() -> Result<()> {
    let isolate = Isolate::new("daemon-stop")?;
    let out = isolate.ffx(&["daemon", "stop"]).await?;
    let want = "Stopped daemon.\n";

    assert_eq!(out.stdout, want);

    Ok(())
}
