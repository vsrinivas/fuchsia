// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    ffx_core::FfxError,
    ffx_daemon::{find_and_connect, is_daemon_running, spawn_daemon},
    ffx_lib_args::Ffx,
    ffx_lib_sub_command::Subcommand,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::DaemonProxy,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
};

async fn get_daemon_proxy() -> Result<DaemonProxy> {
    if !is_daemon_running().await {
        spawn_daemon().await?;
    }
    find_and_connect().await
}

async fn get_remote_proxy() -> Result<RemoteControlProxy> {
    let daemon_proxy = get_daemon_proxy().await?;
    let (remote_proxy, remote_server_end) = create_proxy::<RemoteControlMarker>()?;
    let app: Ffx = argh::from_env();

    daemon_proxy
        .get_remote_control(&app.target.unwrap_or("".to_string()), remote_server_end)
        .await
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
    let is_daemon = is_daemon(&app.subcommand);
    ffx_config::logging::init(is_daemon).await?;
    ffx_lib_suite::ffx_plugin_impl(
        get_daemon_proxy,
        get_remote_proxy,
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
