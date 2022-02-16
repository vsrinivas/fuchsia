// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Result};
use fuchsia_async;
use serde::Serialize;
use std::borrow::Cow;
use std::env;
use std::path::PathBuf;
use std::process::Child;
use std::process::Command;
use std::process::ExitStatus;
use std::process::Stdio;
use tempfile::TempDir;

// TODO(raggi): extract this too, it's a very large dependency to bring in to this lib
use ffx_daemon::is_daemon_running_at_path;

#[derive(Debug)]
pub struct CommandOutput {
    pub status: ExitStatus,
    pub stdout: String,
    pub stderr: String,
}

/// Isolate provides an abstraction around an isolated configuration environment for `ffx`.
pub struct Isolate {
    _tmpdir: TempDir,

    own_path: PathBuf,
    home_dir: PathBuf,
    xdg_config_home: PathBuf,
    pub ascendd_path: PathBuf,
    ssh_key: PathBuf,
}

impl Isolate {
    /// new creates a new isolated environment for ffx to run in, including a
    /// user level configuration that isolates the ascendd socket into a temporary
    /// directory. If $FUCHSIA_TEST_OUTDIR is set, then it is used as the log
    /// directory. The isolated environment is torn down when the Isolate is
    /// dropped, which will attempt to terminate any running daemon and then
    /// remove all isolate files.
    pub async fn new(name: &str) -> Result<Isolate> {
        let tmpdir = tempfile::Builder::new().prefix(name).tempdir()?;
        let home_dir = tmpdir.path().join("user-home");
        let tmp_dir = tmpdir.path().join("tmp");

        let log_dir = if let Ok(d) = std::env::var("FUCHSIA_TEST_OUTDIR") {
            PathBuf::from(d)
        } else {
            tmpdir.path().join("log")
        };

        for dir in [&home_dir, &tmp_dir, &log_dir].iter() {
            std::fs::create_dir_all(dir)?;
        }

        let ascendd_path = tmp_dir.join("ascendd");

        let metrics_path = home_dir.join(".fuchsia/metrics");
        std::fs::create_dir_all(&metrics_path)?;
        // Mark that analytics are disabled
        std::fs::write(metrics_path.join("analytics-status"), "0")?;
        // Mark that the notice has been given
        std::fs::write(metrics_path.join("ffx"), "1")?;

        let xdg_config_home = if cfg!(target_os = "macos") {
            home_dir.join("Library/Preferences")
        } else {
            home_dir.join(".local/share")
        };

        let user_config_dir = xdg_config_home.join("Fuchsia/ffx/config");
        std::fs::create_dir_all(&user_config_dir)?;

        std::fs::write(
            user_config_dir.join("config.json"),
            serde_json::to_string(&UserConfig::for_test(
                log_dir.to_string_lossy(),
                ascendd_path.to_string_lossy(),
            ))?,
        )?;

        std::fs::write(
            user_config_dir.join(".ffx_env"),
            serde_json::to_string(&FfxEnvConfig::for_test(
                user_config_dir.join("config.json").to_string_lossy(),
            ))?,
        )?;

        let ssh_key = ffx_config::get::<String, _>("ssh.priv").await?.into();

        let own_path = Isolate::get_own_path();

        Ok(Isolate { _tmpdir: tmpdir, own_path, home_dir, xdg_config_home, ascendd_path, ssh_key })
    }

    fn get_own_path() -> PathBuf {
        let ffx_path = env::current_exe().expect("could not determine own path");
        // when we daemonize, our path will change to /, so get the canonical path before that occurs.
        std::fs::canonicalize(ffx_path).expect("could not canonicalize own path")
    }

    pub fn ffx_cmd(&self, args: &[&str]) -> std::process::Command {
        let mut cmd = Command::new(&self.own_path);
        cmd.args(args);
        cmd.env_clear();

        // Pass along all temp related variables, so as to avoid anything
        // falling back to writing into /tmp. In our CI environment /tmp is
        // extremely limited, whereas invocations of tests are provided
        // dedicated temporary areas.
        for (var, val) in std::env::vars() {
            if var.contains("TEMP") || var.contains("TMP") {
                cmd.env(var, val);
            }
        }

        cmd.env("HOME", &*self.home_dir);
        cmd.env("XDG_CONFIG_HOME", &*self.xdg_config_home);
        cmd.env("ASCENDD", &*self.ascendd_path);

        // On developer systems, FUCHSIA_SSH_KEY is normally not set, and so ffx
        // looks up an ssh key via a $HOME heuristic, however that is broken by
        // isolation. ffx also however respects the FUCHSIA_SSH_KEY environment
        // variable natively, so, fetching the ssh key path from the config, and
        // then passing that expanded path along explicitly is sufficient to
        // ensure that the isolate has a viable key path.
        cmd.env("FUCHSIA_SSH_KEY", self.ssh_key.to_string_lossy().to_string());

        cmd
    }

    pub fn ffx_spawn(&self, args: &[&str]) -> Result<Child> {
        let mut cmd = self.ffx_cmd(args);
        let child = cmd.stdout(Stdio::null()).stderr(Stdio::null()).spawn()?;
        Ok(child)
    }

    pub async fn ffx(&self, args: &[&str]) -> Result<CommandOutput> {
        let mut cmd = self.ffx_cmd(args);

        fuchsia_async::unblock(move || {
            let out = cmd.output().context("failed to execute")?;
            let stdout = String::from_utf8(out.stdout).context("convert from utf8")?;
            let stderr = String::from_utf8(out.stderr).context("convert from utf8")?;
            Ok::<_, anyhow::Error>(CommandOutput { status: out.status, stdout, stderr })
        })
        .await
    }
}

impl Drop for Isolate {
    fn drop(&mut self) {
        let path = self.ascendd_path.to_string_lossy().to_string();
        if is_daemon_running_at_path(path) {
            let mut cmd = self.ffx_cmd(&["daemon", "stop"]);
            cmd.stdin(Stdio::null());
            cmd.stdout(Stdio::null());
            cmd.stderr(Stdio::null());
            match cmd.spawn().map(|mut child| child.wait()) {
                Ok(_) => {}
                Err(e) => log::info!("Failure calling daemon stop: {:#?}", e),
            }
        }
    }
}

#[derive(Serialize)]
struct UserConfig<'a> {
    log: UserConfigLog<'a>,
    overnet: UserConfigOvernet<'a>,
    test: UserConfigTest,
}

#[derive(Serialize)]
struct UserConfigLog<'a> {
    enabled: bool,
    dir: Cow<'a, str>,
}

#[derive(Serialize)]
struct UserConfigOvernet<'a> {
    socket: Cow<'a, str>,
}

#[derive(Serialize)]
struct UserConfigTest {
    #[serde(rename(serialize = "is-isolated"))]
    is_isolated: bool,
}

impl<'a> UserConfig<'a> {
    fn for_test(dir: Cow<'a, str>, socket: Cow<'a, str>) -> Self {
        Self {
            log: UserConfigLog { enabled: true, dir },
            overnet: UserConfigOvernet { socket },
            test: UserConfigTest { is_isolated: true },
        }
    }
}

#[derive(Serialize)]
struct FfxEnvConfig<'a> {
    user: Cow<'a, str>,
    build: Option<&'static str>,
    global: Option<&'static str>,
}

impl<'a> FfxEnvConfig<'a> {
    fn for_test(user: Cow<'a, str>) -> Self {
        Self { user, build: None, global: None }
    }
}
