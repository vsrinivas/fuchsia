// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate network_manager_cli_lib as network_manager_cli;

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints::{Proxy, ServiceMarker};
use fidl_fuchsia_overnet::{Peer, ServiceConsumerMarker, ServiceConsumerProxy};
use fidl_fuchsia_overnet_protocol::NodeId;
use fidl_fuchsia_router_config::{
    RouterAdminMarker, RouterAdminProxy, RouterStateMarker, RouterStateProxy,
};
use fuchsia_async::{self as fasync, TimeoutExt};
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog as syslog;
use fuchsia_zircon::{self as zx, prelude::DurationNum};
use network_manager_cli::{cli::*, opts::*, printer::Printer};
use std::io::{self};
use std::str;
use structopt::StructOpt;

static OVERNET_TIMEOUT_SEC: i64 = 30;

fn connect() -> Result<(RouterAdminProxy, RouterStateProxy), Error> {
    let router_admin = connect_to_service::<RouterAdminMarker>()
        .context("failed to connect to network manager admin interface")?;
    let router_state = connect_to_service::<RouterStateMarker>()
        .context("failed to connect to network manager interface")?;
    Ok((router_admin, router_state))
}

fn connect_overnet_node(
    svc: &ServiceConsumerProxy,
    name: &str,
    node: &mut NodeId,
) -> Result<fasync::Channel, Error> {
    let (ch0, ch1) = zx::Channel::create()?;
    svc.connect_to_service(node, name, ch0)?;
    fasync::Channel::from_channel(ch1).map_err(|e| e.into())
}

fn supports_network_manager(peer: &Peer) -> bool {
    match peer.description.services {
        None => false,
        Some(ref services) => [RouterAdminMarker::NAME, RouterStateMarker::NAME]
            .iter()
            .map(|svc| services.contains(&svc.to_string()))
            .all(|v| v),
    }
}

async fn connect_overnet() -> Result<(RouterAdminProxy, RouterStateProxy), Error> {
    let svc = connect_to_service::<ServiceConsumerMarker>()?;
    syslog::fx_log_info!("looking for overnet peers...");
    loop {
        let peers = svc.list_peers().await?;
        for mut peer in peers {
            if !supports_network_manager(&peer) {
                continue;
            }
            match (
                connect_overnet_node(&svc, &RouterAdminMarker::NAME, &mut peer.id),
                connect_overnet_node(&svc, &RouterStateMarker::NAME, &mut peer.id),
            ) {
                (Err(_), _) | (_, Err(_)) => {
                    continue;
                }
                (Ok(router_admin_channel), Ok(router_state_channel)) => {
                    syslog::fx_log_info!("connected to peer {:?}", peer.id.id);
                    return Ok((
                        RouterAdminProxy::from_channel(router_admin_channel),
                        RouterStateProxy::from_channel(router_state_channel),
                    ));
                }
            }
        }
    }
}

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["network_manager_cli"]).expect("initialising logging");
    let Opt { overnet, cmd } = Opt::from_args();
    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let fut = async {
        let (router_admin, router_state) = if overnet {
            connect_overnet()
                .on_timeout(fasync::Time::after(OVERNET_TIMEOUT_SEC.seconds()), || {
                    syslog::fx_log_err!("no suitable overnet peers found");
                    Err(format_err!("could not find a suitable overnet peer"))
                })
                .await?
        } else {
            connect()?
        };
        let mut printer = Printer::new(io::stdout());
        run_cmd(cmd, router_admin, router_state, &mut printer).await
    };
    exec.run_singlethreaded(fut)
}
