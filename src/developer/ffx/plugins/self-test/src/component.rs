// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::test::*, anyhow::*};

pub mod include_target {
    use super::*;

    pub(crate) async fn test_list() -> Result<()> {
        let target_nodename = get_target_nodename().await?;
        let isolate = Isolate::new("component-list").await?;

        let out = isolate.ffx(&["--target", &target_nodename, "component", "list"]).await?;

        ensure!(out.status.success(), "status is unexpected: {:?}", out);
        ensure!(!out.stdout.is_empty(), "stdout is unexpectedly empty: {:?}", out);
        ensure!(out.stderr.lines().count() == 0, "stderr is unexpected: {:?}", out);

        Ok(())
    }
}
