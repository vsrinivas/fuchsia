// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use errors::ffx_error;
use ffx_core::ffx_plugin;
use ffx_daemon_start_args::StartCommand;

#[ffx_plugin()]
pub async fn daemon(cmd: StartCommand) -> Result<()> {
    // todo(fxb/108692) remove this use of the global hoist when we put the main one in the environment context
    // instead.
    let hoist = hoist::hoist();
    let ascendd_path = match cmd.path {
        Some(path) => path,
        None => ffx_config::global_env()
            .await
            .and_then(|env| env.get_ascendd_path())
            .map_err(|e| ffx_error!("Could not load daemon socket path: {e:?}"))?,
    };
    let parent_dir =
        ascendd_path.parent().ok_or_else(|| ffx_error!("Daemon socket path had no parent"))?;
    std::fs::create_dir_all(parent_dir).map_err(|e| {
        ffx_error!(
            "Could not create directory for the daemon socket ({path}): {e:?}",
            path = parent_dir.display()
        )
    })?;
    let mut daemon = ffx_daemon::Daemon::new(ascendd_path);
    daemon.start(hoist).await
}
