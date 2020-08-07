// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::logger::setup_logger,
    anyhow::{Context, Result},
    ffx_daemon::{find_and_connect, is_daemon_running, spawn_daemon},
    ffx_lib_args::Ffx,
    ffx_lib_sub_command::Subcommand,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::DaemonProxy,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
};

mod constants;
mod logger;

async fn get_daemon_proxy() -> Result<DaemonProxy> {
    if !is_daemon_running() {
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

fn get_log_name(subcommand: &Subcommand) -> &'static str {
    if let Subcommand::FfxDaemonPlugin(ffx_daemon_plugin_args::DaemonCommand {
        subcommand: ffx_daemon_plugin_sub_command::Subcommand::FfxDaemonStart(_),
    }) = subcommand
    {
        "ffx.daemon"
    } else {
        "ffx"
    }
}

async fn run() -> Result<()> {
    let app: Ffx = argh::from_env();
    setup_logger(get_log_name(&app.subcommand)).await;
    ffx_lib_suite::ffx_plugin_impl(get_daemon_proxy, get_remote_proxy, app).await
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    match run().await {
        Ok(_) => std::process::exit(0),
        Err(err) => {
            eprintln!("BUG: An internal command error occurred.\n{:?}", err);
            std::process::exit(1);
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        ffx_config::{ffx_cmd, ffx_env},
        std::default::Default,
    };

    #[test]
    fn test_config_macros() {
        // Testing these macros outside of the config library.
        let ffx: Ffx = Default::default();
        assert_eq!(ffx, ffx_cmd!());
        let env: Result<()> = ffx_env!();
        assert!(env.is_err());
    }
}
