// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    async_std::future::timeout,
    ffx_core::{ffx_error, FfxError},
    ffx_daemon::{find_and_connect, is_daemon_running, spawn_daemon},
    ffx_lib_args::Ffx,
    ffx_lib_sub_command::Subcommand,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::{DaemonError, DaemonProxy, FastbootMarker, FastbootProxy},
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
    lazy_static::lazy_static,
    std::sync::{Arc, Mutex},
    std::time::Duration,
};

// Config key for event timeout.
const PROXY_TIMEOUT_SECS: &str = "proxy.timeout_secs";
// TODO: a nice way to focus this error message would be to get a list of targets from the daemon
// and be able to distinguish whether there are in fact 0 or multiple available targets.
const TARGET_FAILURE_MSG: &str = "\
We weren't able to open a connection to a target.

Use `ffx target list` to verify the state of connected devices. This error probably means that either:

1) There are no available targets. Make sure your device is connected.
2) There are multiple available targets and you haven't specified a target or provided a default.
Tip: You can use `ffx --target \"my-nodename\" <command>` to specify a target for a particular command, or
use `ffx target default set \"my-nodename\"` if you always want to use a particular target.";

lazy_static! {
    // Using a mutex to guard the spawning of the daemon - the value it contains is not used.
    static ref SPAWN_GUARD: Arc<Mutex<bool>> = Arc::new(Mutex::new(false));
}

// This could get called multiple times by the plugin system via multiple threads - so make sure
// the spawning only happens one thread at a time.
async fn get_daemon_proxy() -> Result<DaemonProxy> {
    {
        let _guard = SPAWN_GUARD.lock().unwrap();
        if !is_daemon_running().await {
            spawn_daemon().await?;
        }
    }
    find_and_connect().await
}

async fn proxy_timeout() -> Result<Duration> {
    let proxy_timeout: ffx_config::Value = ffx_config::get(PROXY_TIMEOUT_SECS).await?;
    Ok(Duration::from_millis(
        (proxy_timeout
            .as_f64()
            .ok_or(anyhow!("unable to convert to float: {:?}", proxy_timeout))?
            * 1000.0) as u64,
    ))
}

async fn get_fastboot_proxy() -> Result<FastbootProxy> {
    let daemon_proxy = get_daemon_proxy().await?;
    let (fastboot_proxy, fastboot_server_end) = create_proxy::<FastbootMarker>()?;
    let app: Ffx = argh::from_env();
    timeout(
        proxy_timeout().await?,
        daemon_proxy
            .get_fastboot(app.target().await?.as_ref().map(|s| s.as_str()), fastboot_server_end),
    )
    .await
    .context("timeout")?
    .context("connecting to Fastboot")
    .map(|_| fastboot_proxy)
}

async fn get_remote_proxy() -> Result<RemoteControlProxy> {
    let daemon_proxy = get_daemon_proxy().await?;
    let (remote_proxy, remote_server_end) = create_proxy::<RemoteControlMarker>()?;
    let app: Ffx = argh::from_env();

    let result = timeout(
        proxy_timeout().await?,
        daemon_proxy.get_remote_control(
            app.target().await?.as_ref().map(|s| s.as_str()),
            remote_server_end,
        ),
    )
    .await
    .context("timeout")?
    .context("connecting to daemon")?;

    match result {
        Ok(_) => Ok(remote_proxy),
        Err(DaemonError::TargetCacheError) => Err(ffx_error!(TARGET_FAILURE_MSG).into()),
        Err(e) => Err(anyhow!("unexpected failure connecting to RCS: {:?}", e)),
    }
}

async fn is_experiment_subcommand_on(key: &'static str) -> bool {
    ffx_config::get(key).await.unwrap_or(false)
}

fn is_daemon(subcommand: &Option<Subcommand>) -> bool {
    if let Some(Subcommand::FfxDaemonPlugin(ffx_daemon_plugin_args::DaemonCommand {
        subcommand: ffx_daemon_plugin_sub_command::Subcommand::FfxDaemonStart(_),
    })) = subcommand
    {
        return true;
    }
    false
}

async fn run() -> Result<()> {
    let app: Ffx = argh::from_env();

    // Configuration initialization must happen before ANY calls to the config (or the cache won't
    // properly have the runtime parameters.
    let overrides = app.runtime_config_overrides();
    ffx_config::init_config(&app.config, &overrides, &app.env)?;
    let log_to_stdio = app.verbose || is_daemon(&app.subcommand);
    ffx_config::logging::init(log_to_stdio).await?;

    // HACK(64402): hoist uses a lazy static initializer obfuscating access to inject
    // this value by other means, so:
    let _ = ffx_config::get("overnet.socket").await.map(|sockpath: String| {
        std::env::set_var("ASCENDD", sockpath);
    });

    ffx_lib_suite::ffx_plugin_impl(
        get_daemon_proxy,
        get_remote_proxy,
        get_fastboot_proxy,
        is_experiment_subcommand_on,
        app,
    )
    .await
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    match run().await {
        Ok(_) => std::process::exit(0),
        Err(err) => {
            if let Some(ffx_err) = err.downcast_ref::<FfxError>() {
                eprintln!("{}", ffx_err);
            } else {
                eprintln!("BUG: An internal command error occurred.\n{:?}", err);
            }
            std::process::exit(1);
        }
    }
}
