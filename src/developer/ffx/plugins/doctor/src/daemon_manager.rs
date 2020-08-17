// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    async_trait::async_trait,
    ffx_daemon::{find_and_connect, is_daemon_running, spawn_daemon},
    fidl_fuchsia_developer_bridge::DaemonProxy,
    std::process::Command,
};

#[async_trait]
pub(crate) trait DaemonManager {
    // Kills any running daemons. Returns a bool indicating whether any daemons were killed.
    fn kill_all(&self) -> Result<bool>;

    async fn is_running(&self) -> bool;

    async fn spawn(&self) -> Result<()>;

    async fn find_and_connect(&self) -> Result<DaemonProxy>;
}

pub(crate) struct DefaultDaemonManager {}

#[async_trait]
impl DaemonManager for DefaultDaemonManager {
    // Kills any running daemons. Returns a bool indicating whether any daemons were killed.
    fn kill_all(&self) -> Result<bool> {
        let status = Command::new("pkill").arg("-f").arg("ffx daemon").status()?;
        return Ok(status.success());
    }

    async fn is_running(&self) -> bool {
        is_daemon_running().await
    }

    async fn spawn(&self) -> Result<()> {
        spawn_daemon().await
    }

    async fn find_and_connect(&self) -> Result<DaemonProxy> {
        find_and_connect().await
    }
}
