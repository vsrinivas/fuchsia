// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::test::*, anyhow::*};

pub(crate) async fn test_not_enabled() -> Result<()> {
    let isolate = Isolate::new("experiment-not-enabled")?;

    let out = isolate.ffx(&["self-test", "experiment"]).await?;

    ensure!(out.stdout.lines().count() == 0, "stdout unexpectedly contains output: {:?}", out);
    ensure!(!out.status.success());
    ensure!(out.stderr.contains("experimental subcommand"), "stderr is unexpected: {:?}", out);
    ensure!(out.stderr.contains("selftest.experiment"), "stderr is unexpected: {:?}", out);

    Ok(())
}

pub(crate) async fn test_enabled() -> Result<()> {
    let isolate = Isolate::new("experiment-enabled")?;

    let _ = isolate.ffx(&["config", "set", "selftest.experiment", "true"]).await?;

    let out = isolate.ffx(&["self-test", "experiment"]).await?;

    ensure!(out.stderr.lines().count() == 0, "stderr is unexpected: {:?}", out);
    ensure!(out.status.success());

    Ok(())
}
