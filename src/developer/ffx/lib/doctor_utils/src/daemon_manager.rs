// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Context, Result},
    async_trait::async_trait,
    ffx_daemon::{find_and_connect, is_daemon_running_at_path, spawn_daemon},
    fidl_fuchsia_developer_ffx::DaemonProxy,
    fuchsia_async::Timer,
    hoist::Hoist,
    std::path::{Path, PathBuf},
    std::process::Command,
    std::time::Duration,
};

const KILL_RETRY_COUNT: usize = 5;
const KILL_RETRY_DELAY: Duration = Duration::from_millis(150);

/// Returns a regex that will find daemon processes started with no socket path or
/// with the socket path given, ignoring any with a different socket path.
fn daemon_start_regex(for_sock: &Path) -> String {
    format!("(^|/)ffx(-.*)? (-.* )?daemon start( --path {path})?$", path = for_sock.display())
}

#[async_trait]
pub trait DaemonManager {
    // Kills any running daemons. Returns a bool indicating whether any daemons were killed.
    async fn kill_all(&self) -> Result<bool>;

    async fn get_pid(&self) -> Result<Vec<usize>>;

    async fn is_running(&self) -> bool;

    async fn spawn(&self) -> Result<()>;

    async fn find_and_connect(&self) -> Result<DaemonProxy>;
}

pub struct DefaultDaemonManager {
    hoist: Hoist,
    socket_path: PathBuf,
}

impl DefaultDaemonManager {
    pub fn new(hoist: Hoist, socket_path: PathBuf) -> Self {
        Self { hoist, socket_path }
    }
}

#[async_trait]
impl DaemonManager for DefaultDaemonManager {
    // Kills any running daemons. Returns a bool indicating whether any daemons were killed.
    async fn kill_all(&self) -> Result<bool> {
        // If ffx was started with a --config or a --target, as fx does, there
        // may be flags between ffx and daemon start.
        // The binary may be ffx, ffx-linux-x64, or ffx-mac-x64.
        let status =
            Command::new("pkill").arg("-f").arg(daemon_start_regex(&self.socket_path)).status()?;

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
        match std::fs::remove_file(&self.socket_path) {
            Ok(_) => tracing::info!("removed ascendd socket at {}", self.socket_path.display()),
            Err(ref e) if e.kind() == std::io::ErrorKind::NotFound => {
                tracing::info!("no existing ascendd socket at {}", self.socket_path.display());
            }
            Err(e) => {
                tracing::info!(
                    "failed to remove ascendd socket at {}: '{}'",
                    self.socket_path.display(),
                    e
                );
            }
        };

        return Ok(status.success());
    }

    // Get the pid of any running daemons.
    async fn get_pid(&self) -> Result<Vec<usize>> {
        // If ffx was started with a --config or a --target, as fx does, there
        // may be flags between ffx and daemon start.
        // The binary may be ffx, ffx-linux-x64, or ffx-mac-x64.
        let cmd =
            Command::new("pgrep").arg("-f").arg(daemon_start_regex(&self.socket_path)).output()?;
        let output: Vec<usize> = String::from_utf8(cmd.stdout)
            .expect("Invalid pgrep output")
            .split("\n")
            .filter_map(|v| match v.parse::<usize>() {
                Ok(val) => Some(val),
                Err(_) => None,
            })
            .collect();

        if !cmd.status.success() {
            match cmd.status.code() {
                Some(1) => (), //Ignore pgrep status for not finding data
                Some(code) => return Err(anyhow!("pgrep status code = {}", code)),
                _ => return Err(anyhow!("pgrep status error")),
            }
        }

        return Ok(output);
    }

    async fn is_running(&self) -> bool {
        is_daemon_running_at_path(&self.socket_path)
    }

    async fn spawn(&self) -> Result<()> {
        let context = ffx_config::global_env_context()
            .context("Trying to spawn daemon with no configuration")?;
        spawn_daemon(&context).await
    }

    async fn find_and_connect(&self) -> Result<DaemonProxy> {
        find_and_connect(&self.hoist, self.socket_path.clone()).await
    }
}
