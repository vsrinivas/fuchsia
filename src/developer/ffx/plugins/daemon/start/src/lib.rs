// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_core::ffx_plugin, ffx_daemon_start_args::StartCommand};

#[ffx_plugin()]
pub async fn daemon(_cmd: StartCommand) -> Result<()> {
    let mut daemon = ffx_daemon::Daemon::new();
    daemon.start().await
}
