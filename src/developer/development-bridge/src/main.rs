// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::logger::setup_logger,
    crate::target_formatter::TargetFormatter,
    anyhow::{anyhow, format_err, Context, Error},
    async_trait::async_trait,
    ffx_command::{Ffx, Subcommand},
    ffx_config::command::exec_config,
    ffx_core::{constants::DAEMON, DaemonProxySource, RemoteControlProxySource},
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

// Cli
pub struct Cli {
    daemon_proxy: DaemonProxy,
}

impl Cli {
    pub async fn new() -> Result<Self, Error> {
        let daemon_proxy = Cli::find_daemon().await?;
        Ok(Self { daemon_proxy })
    }

    pub fn new_with_proxy(daemon_proxy: DaemonProxy) -> Self {
        Self { daemon_proxy }
    }

    async fn find_daemon() -> Result<DaemonProxy, Error> {
        if !is_daemon_running() {
            Cli::spawn_daemon().await?;
        }

        Ok(find_and_connect().await?.expect("No daemon found."))
    }

    pub async fn exec_plugins(&self, subcommand: Subcommand) -> Result<(), Error> {
        ffx_plugins::plugins(self, subcommand).await
    }

    pub async fn list_targets(&self, text: Option<String>) -> Result<Vec<FidlTarget>, Error> {
        match self
            .daemon_proxy
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

    pub async fn quit(&mut self) -> Result<(), Error> {
        self.daemon_proxy.quit().await?;
        println!("Killed daemon.");
        Ok(())
    }

    async fn spawn_daemon() -> Result<(), Error> {
        Command::new(env::current_exe().unwrap()).arg(DAEMON).spawn()?;
        Ok(())
    }
}

#[async_trait]
impl RemoteControlProxySource for Cli {
    async fn get_remote_proxy(&self) -> Result<RemoteControlProxy, Error> {
        let (remote_proxy, remote_server_end) = create_proxy::<RemoteControlMarker>()?;

        let _result = self
            .daemon_proxy
            .get_remote_control(remote_server_end)
            .await
            .context("launch_test call failed")
            .map_err(|e| format_err!("error getting remote: {:?}", e))?;
        Ok(remote_proxy)
    }
}

impl DaemonProxySource for Cli {
    fn get_daemon_proxy(&self) -> &DaemonProxy {
        &self.daemon_proxy
    }
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
    let writer = Box::new(std::io::stdout());
    match app.subcommand {
        Subcommand::List(c) => {
            match Cli::new().await?.list_targets(c.nodename).await {
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
            match Cli::new().await?.quit().await {
                Ok(_) => {}
                Err(e) => {
                    eprintln!("ERROR: {:?}", e);
                }
            }
            Ok(())
        }
        Subcommand::Daemon(_) => start_daemon().await,
        Subcommand::Config(c) => exec_config(c, writer).await,
        _ => Cli::new().await.unwrap().exec_plugins(app.subcommand).await,
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
