// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::logger::setup_logger,
    anyhow::{format_err, Context, Error},
    ffx_command::{Ffx, Subcommand},
    ffx_config::command::exec_config,
    ffx_core::constants::DAEMON,
    ffx_daemon::{find_and_connect, is_daemon_running, start as start_daemon},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::DaemonProxy,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
    std::env,
    std::process::Command,
};

mod logger;

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
        ffx_plugins::plugins(self.get_remote_proxy().await?, subcommand).await
    }

    pub async fn echo(&self, text: Option<String>) -> Result<String, Error> {
        match self
            .daemon_proxy
            .echo_string(match text {
                Some(ref t) => t,
                None => "Ffx",
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

    pub async fn list_targets(&self, text: Option<String>) -> Result<String, Error> {
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

    pub async fn get_remote_proxy(&self) -> Result<RemoteControlProxy, Error> {
        let (remote_proxy, remote_server_end) = create_proxy::<RemoteControlMarker>()?;

        let _result = self
            .daemon_proxy
            .get_remote_control(remote_server_end)
            .await
            .context("launch_test call failed")
            .map_err(|e| format_err!("error getting remote: {:?}", e))?;
        Ok(remote_proxy)
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
        Subcommand::Echo(c) => {
            match Cli::new().await?.echo(c.text).await {
                Ok(r) => {
                    println!("SUCCESS: received {:?}", r);
                }
                Err(e) => {
                    println!("ERROR: {:?}", e);
                }
            }
            Ok(())
        }
        Subcommand::List(c) => {
            match Cli::new().await?.list_targets(c.nodename).await {
                Ok(r) => {
                    let mut r = r.as_str();
                    if r.is_empty() {
                        r = "No devices found.";
                    }
                    println!("{}", r);
                }
                Err(e) => {
                    println!("ERROR: {:?}", e);
                }
            }
            Ok(())
        }
        Subcommand::Quit(_) => {
            match Cli::new().await?.quit().await {
                Ok(_) => {}
                Err(e) => {
                    println!("ERROR: {:?}", e);
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
        async_main().await.map_err(|e| println!("{}", e)).expect("could not start ffx");
    })
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        ffx_config::{ffx_cmd, ffx_env},
        fidl_fuchsia_developer_bridge::{DaemonMarker, DaemonRequest},
        futures::TryStreamExt,
    };

    fn setup_fake_daemon_service() -> DaemonProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();

        hoist::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(DaemonRequest::EchoString { value, responder }) => {
                        let _ = responder.send(value.as_ref());
                    }
                    _ => assert!(false),
                }
            }
        });

        proxy
    }

    #[test]
    fn test_echo() {
        let echo = "test-echo";
        hoist::run(async move {
            let echoed = Cli::new_with_proxy(setup_fake_daemon_service())
                .echo(Some(echo.to_string()))
                .await
                .unwrap();
            assert_eq!(echoed, echo);
        });
    }

    #[test]
    fn test_config_macros() {
        // Testing these macros outside of the config library.
        assert_eq!(
            ffx_cmd!(),
            ffx_command::Ffx {
                config: None,
                subcommand: ffx_command::Subcommand::Daemon(ffx_args::DaemonCommand {}),
            }
        );
        let env: Result<(), Error> = ffx_env!();
        assert!(env.is_err());
    }
}
