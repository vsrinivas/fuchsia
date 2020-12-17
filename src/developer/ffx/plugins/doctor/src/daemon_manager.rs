// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    async_std::fs::remove_file,
    async_trait::async_trait,
    ffx_daemon::{find_and_connect, get_socket, is_daemon_running, spawn_daemon},
    fidl_fuchsia_developer_bridge::DaemonProxy,
    fuchsia_async::Timer,
    std::process::Command,
    std::time::Duration,
};

const KILL_RETRY_COUNT: usize = 5;
const KILL_RETRY_DELAY: Duration = Duration::from_millis(150);

#[async_trait]
pub(crate) trait DaemonManager {
    // Kills any running daemons. Returns a bool indicating whether any daemons were killed.
    async fn kill_all(&self) -> Result<bool>;

    async fn is_running(&self) -> bool;

    async fn spawn(&self) -> Result<()>;

    async fn find_and_connect(&self) -> Result<DaemonProxy>;
}

pub(crate) struct DefaultDaemonManager {}

#[async_trait]
impl DaemonManager for DefaultDaemonManager {
    // Kills any running daemons. Returns a bool indicating whether any daemons were killed.
    async fn kill_all(&self) -> Result<bool> {
        let status = Command::new("pkill").arg("-f").arg("ffx daemon").status()?;

        // There can be a delay between when the daemon is killed and when the
        // ascendd socket closes. If we return too quickly, the main loop will see
        // a PEER_CLOSED instead of not finding a daemon.
        for _ in 0..KILL_RETRY_COUNT {
            if self.is_running().await {
                Timer::new(KILL_RETRY_DELAY).await;
                continue;
            }
            break;
        }

        // TODO(fxbug.dev/66666): Re-evaluate the need for this.
        let sock = get_socket().await;
        match remove_file(&sock).await {
            Ok(_) => log::info!("removed ascendd socket at {}", sock),
            Err(ref e) if e.kind() == async_std::io::ErrorKind::NotFound => {
                log::info!("no existing ascendd socket at {}", sock);
            }
            Err(e) => {
                log::info!("failed to remove ascendd socket at {}: '{}'", sock, e);
            }
        };

        return Ok(status.success());
    }

    async fn is_running(&self) -> bool {
        is_daemon_running().await
    }

    async fn spawn(&self) -> Result<()> {
        spawn_daemon().await
    }

    async fn find_and_connect(&self) -> Result<DaemonProxy> {
        find_and_connect(hoist::hoist()).await
    }
}
