// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_trait::async_trait,
    ffx_daemon::{find_and_connect, is_daemon_running, spawn_daemon},
    fidl_fuchsia_developer_bridge::DaemonProxy,
    std::process::Command,
};

#[async_trait]
pub(crate) trait DaemonManager {
    // Kills any running daemons. Returns a bool indicating whether any daemons were killed.
    fn kill_all(&self) -> Result<bool, Error>;

    fn is_running(&self) -> bool;

    async fn spawn(&self) -> Result<(), Error>;

    async fn find_and_connect(&self) -> Result<Option<DaemonProxy>, Error>;
}

pub(crate) struct DefaultDaemonManager {}

#[async_trait]
impl DaemonManager for DefaultDaemonManager {
    // Kills any running daemons. Returns a bool indicating whether any daemons were killed.
    fn kill_all(&self) -> Result<bool, Error> {
        let status = Command::new("pkill").arg("-f").arg("ffx daemon").status()?;
        return Ok(status.success());
    }

    fn is_running(&self) -> bool {
        return is_daemon_running();
    }

    async fn spawn(&self) -> Result<(), Error> {
        spawn_daemon().await
    }

    async fn find_and_connect(&self) -> Result<Option<DaemonProxy>, Error> {
        find_and_connect().await
    }
}
