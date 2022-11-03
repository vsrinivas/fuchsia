// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Result};
use fuchsia_async;
use serde::Serialize;
use std::borrow::Cow;
use std::collections::HashMap;
use std::path::PathBuf;
use std::process::Child;
use std::process::ExitStatus;
use std::process::Stdio;
use std::time::SystemTime;
use tempfile::TempDir;

#[derive(Debug)]
pub struct CommandOutput {
    pub status: ExitStatus,
    pub stdout: String,
    pub stderr: String,
}

/// Isolate provides an abstraction around an isolated configuration environment for `ffx`.
pub struct Isolate {
    tmpdir: TempDir,
    env_ctx: ffx_config::EnvironmentContext,
}

impl Isolate {
    /// new creates a new isolated environment for ffx to run in, including a
    /// user level configuration that isolates the ascendd socket into a temporary
    /// directory. If $FUCHSIA_TEST_OUTDIR is set, then it is used as the log
    /// directory. The isolated environment is torn down when the Isolate is
    /// dropped, which will attempt to terminate any running daemon and then
    /// remove all isolate files.
    pub async fn new(name: &str, ffx_path: PathBuf, ssh_key: PathBuf) -> Result<Isolate> {
        let tmpdir = tempfile::Builder::new().prefix(name).tempdir()?;

        let log_dir = if let Ok(d) = std::env::var("FUCHSIA_TEST_OUTDIR") {
            PathBuf::from(d)
        } else {
            tmpdir.path().join("log")
        };

        std::fs::create_dir_all(&log_dir)?;
        let metrics_path = tmpdir.path().join("metrics_home/.fuchsia/metrics");
        std::fs::create_dir_all(&metrics_path)?;

        // TODO(114011): See if we should get isolate-dir itself to deal with metrics isolation.

        // Mark that analytics are disabled
        std::fs::write(metrics_path.join("analytics-status"), "0")?;
        // Mark that the notice has been given
        std::fs::write(metrics_path.join("ffx"), "1")?;

        let mut mdns_discovery = true;
        let mut target_addr = None;
        if let Ok(addr) = std::env::var("FUCHSIA_DEVICE_ADDR") {
            // When run in infra, disable mdns discovery.
            // TODO(fxbug.dev/44710): Remove when we have proper network isolation.
            target_addr = Option::Some(Cow::Owned(addr.to_string() + &":0".to_string()));
            mdns_discovery = false;
        }
        std::fs::write(
            tmpdir.path().join(".ffx_user_config.json"),
            serde_json::to_string(&UserConfig::for_test(
                log_dir.to_string_lossy(),
                target_addr,
                mdns_discovery,
            ))?,
        )?;

        std::fs::write(
            tmpdir.path().join(".ffx_env"),
            serde_json::to_string(&FfxEnvConfig::for_test(
                tmpdir.path().join(".ffx_user_config.json").to_string_lossy(),
            ))?,
        )?;

        let mut env_vars = HashMap::new();

        // Pass along all temp related variables, so as to avoid anything
        // falling back to writing into /tmp. In our CI environment /tmp is
        // extremely limited, whereas invocations of tests are provided
        // dedicated temporary areas.
        for (var, val) in std::env::vars() {
            if var.contains("TEMP") || var.contains("TMP") {
                let _ = env_vars.insert(var, val);
            }
        }

        let _ = env_vars.insert(
            "HOME".to_owned(),
            tmpdir.path().join("metrics_home").to_string_lossy().to_string(),
        );

        let _ = env_vars.insert(
            ffx_config::EnvironmentContext::FFX_BIN_ENV.to_owned(),
            ffx_path.to_string_lossy().to_string(),
        );

        // On developer systems, FUCHSIA_SSH_KEY is normally not set, and so ffx
        // looks up an ssh key via a $HOME heuristic, however that is broken by
        // isolation. ffx also however respects the FUCHSIA_SSH_KEY environment
        // variable natively, so, fetching the ssh key path from the config, and
        // then passing that expanded path along explicitly is sufficient to
        // ensure that the isolate has a viable key path.
        let _ =
            env_vars.insert("FUCHSIA_SSH_KEY".to_owned(), ssh_key.to_string_lossy().to_string());

        let env_ctx = ffx_config::EnvironmentContext::isolated(
            tmpdir.path().to_owned(),
            env_vars,
            ffx_config::ConfigMap::new(),
            Some(tmpdir.path().join(".ffx_env").to_owned()),
        );

        Ok(Isolate { tmpdir, env_ctx })
    }

    pub fn ascendd_path(&self) -> PathBuf {
        self.tmpdir.path().join("daemon.sock")
    }

    pub fn ffx_cmd(&self, args: &[&str]) -> Result<std::process::Command> {
        let mut cmd = self.env_ctx.rerun_prefix()?;
        cmd.args(args);
        Ok(cmd)
    }

    pub fn ffx_spawn(&self, args: &[&str]) -> Result<Child> {
        let mut cmd = self.ffx_cmd(args)?;
        let child = cmd.stdout(Stdio::null()).stderr(Stdio::null()).spawn()?;
        Ok(child)
    }

    pub async fn ffx(&self, args: &[&str]) -> Result<CommandOutput> {
        let mut cmd = self.ffx_cmd(args)?;

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
        match self.ffx_cmd(&["daemon", "stop"]) {
            Ok(mut cmd) => {
                cmd.stdin(Stdio::null());
                cmd.stdout(Stdio::null());
                cmd.stderr(Stdio::null());
                match cmd.spawn().map(|mut child| child.wait()) {
                    Ok(_) => {}
                    Err(e) => tracing::info!("Failure calling daemon stop: {:#?}", e),
                }
            }
            Err(e) => tracing::info!("Failure forming daemon stop command: {:#?}", e),
        }
    }
}

#[derive(Serialize)]
struct UserConfig<'a> {
    log: UserConfigLog<'a>,
    test: UserConfigTest,
    targets: UserConfigTargets<'a>,
    discovery: UserConfigDiscovery,
}

#[derive(Serialize)]
struct UserConfigLog<'a> {
    enabled: bool,
    dir: Cow<'a, str>,
}

#[derive(Serialize)]
struct UserConfigTest {
    #[serde(rename(serialize = "is-isolated"))]
    is_isolated: bool,
}

#[derive(Serialize)]
struct UserConfigTargets<'a> {
    manual: HashMap<Cow<'a, str>, Option<SystemTime>>,
}

#[derive(Serialize)]
struct UserConfigDiscovery {
    mdns: UserConfigMdns,
}

#[derive(Serialize)]
struct UserConfigMdns {
    enabled: bool,
}

impl<'a> UserConfig<'a> {
    fn for_test(dir: Cow<'a, str>, target: Option<Cow<'a, str>>, discovery: bool) -> Self {
        let mut manual_targets = HashMap::new();
        if !target.is_none() {
            manual_targets.insert(target.unwrap(), None);
        }
        Self {
            log: UserConfigLog { enabled: true, dir },
            test: UserConfigTest { is_isolated: true },
            targets: UserConfigTargets { manual: manual_targets },
            discovery: UserConfigDiscovery { mdns: UserConfigMdns { enabled: discovery } },
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
