// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{assert_eq, test::*};
use anyhow::*;
use fuchsia_async::Duration;

pub(crate) async fn test_echo() -> Result<()> {
    let isolate = new_isolate("daemon-echo").await?;
    let out = isolate.ffx(&["daemon", "echo"]).await?;

    let want = "SUCCESS: received \"Ffx\"\n";
    assert_eq!(out.stdout, want);

    Ok(())
}

pub(crate) async fn test_config_flag() -> Result<()> {
    let isolate = new_isolate("daemon-config-flag").await?;
    let mut daemon = isolate.ffx_spawn(&["daemon", "start"])?;

    // wait a bit to make sure the daemon has had a chance to start up, then check that it's
    // still running
    fuchsia_async::Timer::new(Duration::from_millis(100)).await;
    assert_eq!(None, daemon.try_wait()?, "Daemon didn't stay up for at least 100ms after starting");

    // This should not terminate the daemon just started, as it won't
    // share an overnet socket with it.
    let mut ascendd_path2 = isolate.ascendd_path().clone();
    ascendd_path2.set_extension("2");
    let _out = isolate
        .ffx(&[
            "--config",
            &format!("overnet.socket={}", ascendd_path2.to_string_lossy()),
            "daemon",
            "stop",
        ])
        .await?;

    // wait a bit again because the daemon doesn't immediately exit when it gets a stop message
    fuchsia_async::Timer::new(Duration::from_millis(200)).await;
    assert_eq!(
        None,
        daemon.try_wait()?,
        "Daemon didn't stay up after the stop message was sent to the other daemon."
    );

    let _out = isolate.ffx(&["daemon", "stop"]).await?;
    fuchsia_async::unblock(move || daemon.wait()).await?;

    Ok(())
}

pub(crate) async fn test_stop() -> Result<()> {
    let isolate = new_isolate("daemon-stop").await?;
    let out = isolate.ffx(&["daemon", "stop"]).await?;
    let want = "Stopped daemon.\n";

    assert_eq!(out.stdout, want);

    Ok(())
}
