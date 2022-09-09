// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_core::ffx_plugin, ffx_daemon_start_args::StartCommand};

#[ffx_plugin()]
pub async fn daemon(cmd: StartCommand) -> Result<()> {
    // todo(fxb/108692) remove this use of the global hoist when we put the main one in the environment context
    // instead.
    let hoist = hoist::hoist();
    let ascendd_path = match cmd.path {
        Some(path) => path,
        None => ffx_config::global_env().await?.get_ascendd_path()?,
    };
    let mut daemon = ffx_daemon::Daemon::new(ascendd_path);
    daemon.start(hoist).await
}
