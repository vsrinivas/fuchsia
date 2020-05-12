// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args::{Ffx, Subcommand},
    crate::config::command::exec_config,
    crate::constants::DAEMON,
    crate::daemon::{is_daemon_running, start as start_daemon},
    crate::logger::setup_logger,
    anyhow::{format_err, Context, Error},
    ffx_run_component::{args::RunComponentCommand, run_component},
    ffx_test::{args::TestCommand, test},
    ffxlib::find_and_connect,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::DaemonProxy,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
    std::env,
    std::process::Command,
};

mod args;
mod config;
mod constants;
mod daemon;
mod discovery;
mod logger;
mod mdns;
mod net;
mod onet;
mod ssh;
mod target;
mod util;

// Cli
pub struct Cli {
    daemon_proxy: DaemonProxy,
}

impl Cli {
    pub async fn new() -> Result<Self, Error> {
        setup_logger("ffx").await;
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

    pub async fn run_component(&mut self, run_cmd: RunComponentCommand) -> Result<(), Error> {
        run_component(self.get_remote_proxy().await?, run_cmd).await
    }

    pub async fn test(&mut self, test_cmd: TestCommand) -> Result<(), Error> {
        test(self.get_remote_proxy().await?, test_cmd).await
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

async fn async_main() -> Result<(), Error> {
    let app: Ffx = argh::from_env();
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
        Subcommand::RunComponent(c) => {
            match Cli::new().await?.run_component(c).await {
                Ok(_) => {}
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
        Subcommand::Test(t) => {
            match Cli::new().await.unwrap().test(t).await {
                Ok(_) => {
                    log::info!("Test successfully run");
                }
                Err(e) => {
                    println!("ERROR: {:?}", e);
                }
            }
            Ok(())
        }
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
}
