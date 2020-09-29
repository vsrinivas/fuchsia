// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    async_std::future::timeout,
    ffx_core::FfxError,
    ffx_daemon::{find_and_connect, is_daemon_running, spawn_daemon},
    ffx_lib_args::Ffx,
    ffx_lib_sub_command::Subcommand,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::{DaemonProxy, FastbootMarker, FastbootProxy},
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
    lazy_static::lazy_static,
    std::sync::{Arc, Mutex},
    std::time::Duration,
};

// Config key for event timeout.
const PROXY_TIMEOUT_SECS: &str = "proxy.timeout_secs";

lazy_static! {
    // Using a mutex to guard the spawning of the daemon - the value it contains is not used.
    static ref SPAWN_GUARD: Arc<Mutex<bool>> = Arc::new(Mutex::new(false));
}

// This could get called multiple times by the plugin system via multiple threads - so make sure
// the spawning only happens one thread at a time.
async fn get_daemon_proxy() -> Result<DaemonProxy> {
    if !is_daemon_running().await {
        let _guard = SPAWN_GUARD.lock().unwrap();
        // check again now that we have the guard.
        if !is_daemon_running().await {
            spawn_daemon().await?;
        }
    }
    find_and_connect().await
}

async fn get_fastboot_proxy() -> Result<FastbootProxy> {
    let daemon_proxy = get_daemon_proxy().await?;
    let (fastboot_proxy, fastboot_server_end) = create_proxy::<FastbootMarker>()?;
    let app: Ffx = argh::from_env();

    daemon_proxy
        .get_fastboot(&app.target.unwrap_or("".to_string()), fastboot_server_end)
        .await
        .context("connecting to Fastboot")
        .map(|_| fastboot_proxy)
}

async fn get_remote_proxy() -> Result<RemoteControlProxy> {
    let daemon_proxy = get_daemon_proxy().await?;
    let (remote_proxy, remote_server_end) = create_proxy::<RemoteControlMarker>()?;
    let app: Ffx = argh::from_env();
    let event_timeout = Duration::from_secs(ffx_config::get(PROXY_TIMEOUT_SECS).await?);
    timeout(
        event_timeout,
        daemon_proxy.get_remote_control(&app.target.unwrap_or("".to_string()), remote_server_end),
    )
    .await
    .context("timeout")?
    .context("connecting to RCS")
    .map(|_| remote_proxy)
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
    ffx_config::init_config(&app.config, &app.env)?;
    let log_to_stdio = app.verbose || is_daemon(&app.subcommand);
    ffx_config::logging::init(log_to_stdio).await?;
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
