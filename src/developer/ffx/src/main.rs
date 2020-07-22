// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::logger::setup_logger,
    anyhow::{format_err, Context, Error},
    ffx_daemon::{find_and_connect, is_daemon_running, spawn_daemon},
    ffx_lib_args::Ffx,
    ffx_lib_sub_command::Subcommand,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::DaemonProxy,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
};

mod logger;

async fn get_daemon_proxy() -> Result<DaemonProxy, Error> {
    if !is_daemon_running().await {
        spawn_daemon().await?;
    }
    Ok(find_and_connect().await?.expect("No daemon found."))
}

async fn get_remote_proxy() -> Result<RemoteControlProxy, Error> {
    let daemon_proxy = get_daemon_proxy().await?;
    let (remote_proxy, remote_server_end) = create_proxy::<RemoteControlMarker>()?;
    let app: Ffx = argh::from_env();

    let _result = daemon_proxy
        .get_remote_control(&app.target.unwrap_or("".to_string()), remote_server_end)
        .await
        .context("get_remote_control call failed")
        .map_err(|e| format_err!("error getting remote: {:?}", e))?;
    Ok(remote_proxy)
}

////////////////////////////////////////////////////////////////////////////////
// main
fn get_log_name(subcommand: &Subcommand) -> &'static str {
    if let Subcommand::FfxDaemonSuite(ffx_daemon_suite_args::DaemonCommand {
        subcommand: ffx_daemon_suite_sub_command::Subcommand::FfxDaemonStart(_),
    }) = subcommand
    {
        "ffx.daemon"
    } else {
        "ffx"
    }
}

async fn run() -> Result<(), Error> {
    let app: Ffx = argh::from_env();
    setup_logger(get_log_name(&app.subcommand)).await;
    ffx_lib::ffx_plugin_impl(get_daemon_proxy, get_remote_proxy, app).await
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    match run().await {
        Ok(_) => std::process::exit(0),
        Err(err) => {
            eprintln!("BUG: An internal command error occurred.\nError:\n\t{}\nCause:", err);
            err.chain().skip(1).for_each(|cause| eprintln!("\t{}", cause));
            std::process::exit(1);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        ffx_config::constants::{ASCENDD_SOCKET, LOG_ENABLED},
        ffx_config::{ffx_cmd, ffx_env, get},
        serde_json::Value,
    };

    #[test]
    fn test_config_macros() {
        // Testing these macros outside of the config library.
        assert_eq!(ffx_cmd!(), ffx_lib_args::DEFAULT_FFX);
        let env: Result<(), Error> = ffx_env!();
        assert!(env.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_config_defaults_get() -> Result<(), Error> {
        // Testing these macros outside of the config library to make sure test values are
        // returned.
        assert_eq!(
            get!(ASCENDD_SOCKET).await?,
            Some(Value::String("/tmp/ascendd_for_testing".to_string()))
        );
        assert_eq!(get!(LOG_ENABLED).await?, Some(Value::Bool(true)));
        Ok(())
    }
}
