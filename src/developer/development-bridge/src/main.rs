// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args::{Fdb, Subcommand},
    failure::{self, Error, ResultExt},
    fidl::endpoints::ServiceMarker,
    fidl_fidl_developer_bridge as bridge,
    fidl_fuchsia_overnet::ServiceConsumerProxyInterface,
    std::env::current_exe,
    std::process::{Command, Stdio},
};

mod constants;
use constants::{CONFIG_JSON_FILE, DAEMON};

mod config;
use config::Config;

mod args;

async fn exec_start() -> Result<(), Error> {
    log::info!("Starting background daemon...");
    let mut path = current_exe().unwrap();
    path.pop();
    path.push(DAEMON);
    Command::new(path).stdout(Stdio::null()).stderr(Stdio::null()).spawn()?;
    Ok(())
}

async fn exec_echo_client(text: Option<String>) -> Result<(), Error> {
    let svc = hoist::connect_as_service_consumer()?;
    loop {
        let peers = svc.list_peers().await?;
        log::trace!("Got peers: {:?}", peers);
        for mut peer in peers {
            if peer.description.services.is_none() {
                continue;
            }
            if peer
                .description
                .services
                .unwrap()
                .iter()
                .find(|name| *name == bridge::DaemonMarker::NAME)
                .is_none()
            {
                continue;
            }

            log::trace!("Trying peer: {:?}", peer.id);

            let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
            if let Err(e) = svc.connect_to_service(&mut peer.id, bridge::DaemonMarker::NAME, s) {
                log::trace!("{:?}", e);
                continue;
            }
            let proxy =
                fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
            let cli = bridge::DaemonProxy::new(proxy);
            let echo: &str = match text {
                Some(ref t) => t,
                None => "Fdb",
            };
            log::info!("Sending {:?} to {:?}", echo, peer.id);
            match cli.echo_string(echo).await {
                Ok(r) => {
                    log::info!("SUCCESS: received {:?}", r);
                    return Ok(());
                }
                Err(e) => {
                    log::info!("ERROR: {:?}", e);
                    continue;
                }
            };
        }
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
        log::info!("Connected peer: {:?}", peer);
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// main

async fn async_main() -> Result<(), Error> {
    let app: Fdb = argh::from_env();
    let mut config: Config = Config::new();
    if let Err(err) = config.load_from_config_data(CONFIG_JSON_FILE) {
        log::error!("Failed to load configuration file: {}", err);
    }

    match app.subcommand {
        Subcommand::Start(_) => exec_start().await,
        Subcommand::Echo(c) => exec_echo_client(c.text).await,
        Subcommand::List(_) => exec_list().await,
    }
}

fn main() {
    hoist::run(async move {
        async_main().await.map_err(|e| log::error!("{}", e)).expect("could not start fdb");
    })
}
