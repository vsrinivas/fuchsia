// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(unused)]
use {
    crate::args::{Fdb, Subcommand},
    crate::config::Config,
    crate::constants::{CONFIG_JSON_FILE, MAX_RETRY_COUNT},
    anyhow::{Context, Error},
    fidl::endpoints::ServiceMarker,
    fidl_fidl_developer_bridge::{DaemonMarker, DaemonProxy},
    fidl_fuchsia_developer_remotecontrol::RunComponentResponse,
    fidl_fuchsia_overnet::ServiceConsumerProxyInterface,
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::TryStreamExt,
    std::process::{Command, Stdio},
};

mod args;
mod config;
mod constants;

// Cli
pub struct Cli {
    daemon_proxy: DaemonProxy,
}

impl Cli {
    pub async fn new() -> Result<Cli, Error> {
        let mut peer_id = Cli::find_daemon().await?;
        let daemon_proxy = Cli::create_daemon_proxy(&mut peer_id).await?;
        Ok(Cli { daemon_proxy })
    }

    pub fn new_with_proxy(daemon_proxy: DaemonProxy) -> Cli {
        Cli { daemon_proxy }
    }

    async fn create_daemon_proxy(id: &mut NodeId) -> Result<DaemonProxy, Error> {
        let svc = hoist::connect_as_service_consumer()?;
        let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
        svc.connect_to_service(id, DaemonMarker::NAME, s)?;
        let proxy = fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
        Ok(DaemonProxy::new(proxy))
    }

    async fn find_daemon() -> Result<NodeId, Error> {
        let svc = hoist::connect_as_service_consumer()?;
        // Sometimes list_peers doesn't properly report the published services - retry a few times
        // but don't loop indefinitely.
        for _ in 0..MAX_RETRY_COUNT {
            let peers = svc.list_peers().await?;
            log::trace!("Got peers: {:?}", peers);
            for peer in peers {
                if peer.description.services.is_none() {
                    continue;
                }
                if peer
                    .description
                    .services
                    .unwrap()
                    .iter()
                    .find(|name| *name == DaemonMarker::NAME)
                    .is_none()
                {
                    continue;
                }
                return Ok(peer.id);
            }
        }
        panic!("No daemon found.")
    }

    pub async fn echo<'a>(&'a self, text: Option<String>) -> Result<String, Error> {
        match self
            .daemon_proxy
            .echo_string(match text {
                Some(ref t) => t,
                None => "Fdb",
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

    pub async fn run_component<'a>(
        &'a self,
        url: String,
        args: &Vec<String>,
    ) -> Result<RunComponentResponse, Error> {
        match self.daemon_proxy.run_component(&url, &mut args.iter().map(|s| s.as_str())).await {
            Ok(r) => {
                log::info!("SUCCESS: received {:?}", r);
                return Ok(r);
            }
            Err(e) => panic!("ERROR: {:?}", e),
        }
    }
}

async fn exec_start(config: &Config) -> Result<(), Error> {
    println!("Starting background daemon");
    Command::new(config.get_daemon_path()).stdout(Stdio::null()).stderr(Stdio::null()).spawn()?;
    Ok(())
}

async fn exec_list() -> Result<(), Error> {
    let svc = hoist::connect_as_service_consumer()?;
    let peers = svc.list_peers().await?;
    for peer in peers {
        if peer.description.services.is_none() {
            continue;
        }
        if peer.is_self {
            continue;
        }
        println!("Connected peer: {:?}", peer);
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// main

async fn async_main() -> Result<(), Error> {
    let app: Fdb = argh::from_env();
    let mut config: Config = Config::new();
    let _ = config.load_from_config_data(CONFIG_JSON_FILE);
    match app.subcommand {
        Subcommand::Start(_) => exec_start(&config).await,
        Subcommand::Echo(c) => {
            match Cli::new().await.unwrap().echo(c.text).await {
                Ok(r) => {
                    println!("SUCCESS: received {:?}", r);
                }
                Err(e) => {
                    println!("ERROR: {:?}", e);
                }
            }
            Ok(())
        }
        Subcommand::List(_) => exec_list().await,
        Subcommand::RunComponent(c) => {
            match Cli::new().await.unwrap().run_component(c.url, &c.args).await {
                Ok(r) => {
                    println!("SUCCESS: received {:?}", r);
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
        async_main().await.map_err(|e| log::error!("{}", e)).expect("could not start fdb");
    })
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fidl_developer_bridge::{DaemonMarker, DaemonProxy, DaemonRequest};

    fn setup_fake_daemon_service() -> DaemonProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();

        hoist::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(DaemonRequest::EchoString { value, responder }) => {
                        let _ = responder.send(value.as_ref());
                    }
                    Some(DaemonRequest::RunComponent { component_url, args, responder }) => {
                        let response = RunComponentResponse {
                            component_stdout: Some(component_url),
                            component_stderr: Some(args.join(",")),
                            exit_code: Some(0),
                        };
                        let _ = responder.send(response);
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
    fn test_run_component() {
        let url = "http://test.com";
        let args = vec!["test1".to_string(), "test2".to_string()];
        hoist::run(async move {
            let response = Cli::new_with_proxy(setup_fake_daemon_service())
                .run_component(url.to_string(), &args)
                .await
                .unwrap();
            assert_eq!(response.exit_code.unwrap(), 0);
            assert_eq!(response.component_stdout.unwrap(), url.to_string());
            assert_eq!(response.component_stderr.unwrap(), args.join(","));
        });
    }
}
