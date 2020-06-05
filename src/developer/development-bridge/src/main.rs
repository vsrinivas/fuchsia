// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::logger::setup_logger,
    crate::target_formatter::TargetFormatter,
    anyhow::{anyhow, format_err, Context, Error},
    ffx_command::{Ffx, Subcommand},
    ffx_core::constants::DAEMON,
    ffx_daemon::{find_and_connect, is_daemon_running, start as start_daemon},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::{DaemonProxy, Target as FidlTarget},
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
    std::convert::TryFrom,
    std::env,
    std::process::Command,
};

mod logger;
mod target_formatter;

async fn spawn_daemon() -> Result<(), Error> {
    Command::new(env::current_exe().unwrap()).arg(DAEMON).spawn()?;
    Ok(())
}

async fn get_daemon_proxy() -> Result<DaemonProxy, Error> {
    if !is_daemon_running() {
        spawn_daemon().await?;
    }
    Ok(find_and_connect().await?.expect("No daemon found."))
}

async fn list_targets(
    daemon_proxy: DaemonProxy,
    text: Option<String>,
) -> Result<Vec<FidlTarget>, Error> {
    match daemon_proxy
        .list_targets(match text {
            Some(ref t) => t,
            None => "",
        })
        .await
    {
        Ok(r) => {
            log::info!("SUCCESS: received {:?}", r);
            return Ok(r);
        }
        Err(e) => panic!("ERROR: {:?}", e),
    }
}

async fn exec_plugins<D, DFut, R, RFut>(
    daemon_factory: D,
    remote_factory: R,
    subcommand: Subcommand,
) -> Result<(), Error>
where
    D: FnOnce() -> DFut,
    DFut: std::future::Future<
        Output = std::result::Result<fidl_fuchsia_developer_bridge::DaemonProxy, anyhow::Error>,
    >,
    R: FnOnce() -> RFut,
    RFut: std::future::Future<
        Output = std::result::Result<
            fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
            anyhow::Error,
        >,
    >,
{
    ffx_plugins::plugins(daemon_factory, remote_factory, subcommand).await
}

async fn quit(daemon_proxy: DaemonProxy) -> Result<(), Error> {
    daemon_proxy.quit().await?;
    println!("Killed daemon.");
    Ok(())
}

async fn get_remote_proxy() -> Result<RemoteControlProxy, Error> {
    let daemon_proxy = get_daemon_proxy().await?;
    let (remote_proxy, remote_server_end) = create_proxy::<RemoteControlMarker>()?;

    let _result = daemon_proxy
        .get_remote_control(remote_server_end)
        .await
        .context("launch_test call failed")
        .map_err(|e| format_err!("error getting remote: {:?}", e))?;
    Ok(remote_proxy)
}

////////////////////////////////////////////////////////////////////////////////
// main
fn get_log_name(subcommand: &Subcommand) -> &'static str {
    if let Subcommand::Daemon(_) = subcommand {
        "ffx.daemon"
    } else {
        "ffx"
    }
}

async fn async_main() -> Result<(), Error> {
    let app: Ffx = argh::from_env();
    setup_logger(get_log_name(&app.subcommand)).await;
    match app.subcommand {
        Subcommand::List(c) => {
            match list_targets(get_daemon_proxy().await?, c.nodename).await {
                Ok(r) => match r.len() {
                    0 => println!("No devices found."),
                    _ => {
                        let formatter = TargetFormatter::try_from(r)
                            .map_err(|e| anyhow!("target malformed: {:?}", e))?;
                        println!("{}", formatter.lines().join("\n"));
                    }
                },
                Err(e) => {
                    eprintln!("ERROR: {:?}", e);
                }
            }
            Ok(())
        }
        Subcommand::Quit(_) => {
            match quit(get_daemon_proxy().await?).await {
                Ok(_) => {}
                Err(e) => {
                    eprintln!("ERROR: {:?}", e);
                }
            }
            Ok(())
        }
        Subcommand::Daemon(_) => start_daemon().await,
        _ => exec_plugins(get_daemon_proxy, get_remote_proxy, app.subcommand).await,
    }
}

fn main() {
    hoist::run(async move {
        match async_main().await {
            Ok(_) => std::process::exit(0),
            Err(err) => {
                eprintln!("BUG: An internal command error occurred.\nError:\n\t{}\nCause:", err);
                err.chain().skip(1).for_each(|cause| eprintln!("\t{}", cause));
                std::process::exit(1);
            }
        }
    })
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        ffx_config::{ffx_cmd, ffx_env},
        ffx_core::args::DaemonCommand,
    };

    #[test]
    fn test_config_macros() {
        // Testing these macros outside of the config library.
        assert_eq!(
            ffx_cmd!(),
            Ffx { config: None, subcommand: Subcommand::Daemon(DaemonCommand {}) }
        );
        let env: Result<(), Error> = ffx_env!();
        assert!(env.is_err());
    }
}
