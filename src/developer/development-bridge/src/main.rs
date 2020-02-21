// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(unused)]
use {
    crate::args::{Ffx, Subcommand},
    crate::config::command::exec_config,
    crate::constants::{CONFIG_JSON_FILE, DAEMON, MAX_RETRY_COUNT},
    anyhow::{Context, Error},
    ffx_daemon::{is_daemon_running, start as start_daemon},
    fidl::endpoints::{create_proxy, ServiceMarker},
    fidl_fidl_developer_bridge::{DaemonMarker, DaemonProxy},
    fidl_fuchsia_developer_remotecontrol::{ComponentControllerEvent, ComponentControllerMarker},
    fidl_fuchsia_overnet::ServiceConsumerProxyInterface,
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::{StreamExt, TryStreamExt},
    std::env,
    std::process::Command,
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
        if !is_daemon_running() {
            Cli::spawn_daemon().await?;
        }
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

    pub async fn run_component<'a>(&'a self, url: String, args: &Vec<String>) -> Result<(), Error> {
        let (proxy, server_end) = create_proxy::<ComponentControllerMarker>()?;
        let (sout, cout) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;
        let (serr, cerr) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;

        // This is only necessary until Overnet correctly handle setup for passed channels.
        // TODO(jwing) remove this once that is finished.
        proxy.ping();

        let out_thread = std::thread::spawn(move || loop {
            let mut buf = [0u8; 128];
            let n = cout.read(&mut buf).or::<usize>(Ok(0usize)).unwrap();
            if n > 0 {
                print!("{}", String::from_utf8_lossy(&buf));
            }
        });

        let err_thread = std::thread::spawn(move || loop {
            let mut buf = [0u8; 128];
            let n = cerr.read(&mut buf).or::<usize>(Ok(0usize)).unwrap();
            if n > 0 {
                eprint!("{}", String::from_utf8_lossy(&buf));
            }
        });

        let term_thread = std::thread::spawn(move || {
            let mut e = proxy.take_event_stream().take(1usize);
            while let Some(result) = futures::executor::block_on(e.next()) {
                match result {
                    Ok(ComponentControllerEvent::OnTerminated { exit_code }) => {
                        println!("Component exited with exit code: {}", exit_code);
                        break;
                    }
                    Err(err) => {
                        eprintln!("error reading component controller events. Component termination may not be detected correctly. {} ", err);
                    }
                }
            }
        });

        let _result = self
            .daemon_proxy
            .start_component(&url, &mut args.iter().map(|s| s.as_str()), sout, serr, server_end)
            .await?;
        term_thread.join().unwrap();

        Ok(())
    }

    async fn spawn_daemon() -> Result<(), Error> {
        Command::new(env::current_exe().unwrap()).arg(DAEMON).spawn()?;
        Ok(())
    }
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
    let app: Ffx = argh::from_env();
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
        Subcommand::List(_) => exec_list().await,
        Subcommand::RunComponent(c) => {
            match Cli::new().await?.run_component(c.url, &c.args).await {
                Ok(r) => {}
                Err(e) => {
                    println!("ERROR: {:?}", e);
                }
            }
            Ok(())
        }
        Subcommand::Daemon(_) => start_daemon().await,
        Subcommand::Config(c) => exec_config(c),
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
                    Some(DaemonRequest::StartComponent {
                        component_url,
                        args,
                        component_stdout: _,
                        component_stderr: _,
                        controller: _,
                        responder,
                    }) => {
                        let _ = responder.send(&mut Ok(()));
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
    fn test_run_component() -> Result<(), Error> {
        let url = "fuchsia-pkg://fuchsia.com/test#meta/test.cmx";
        let args = vec!["test1".to_string(), "test2".to_string()];
        let (daemon_proxy, stream) = fidl::endpoints::create_proxy_and_stream::<DaemonMarker>()?;
        let (_, server_end) = create_proxy::<ComponentControllerMarker>()?;
        let (sout, _) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;
        let (serr, _) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;
        hoist::run(async move {
            // There isn't a lot we can test here right now since this method has an empty response.
            // We just check for an Ok(()) and leave it to a real integration to test behavior.
            let response = Cli::new_with_proxy(setup_fake_daemon_service())
                .run_component(url.to_string(), &args)
                .await
                .unwrap();
        });

        Ok(())
    }
}
