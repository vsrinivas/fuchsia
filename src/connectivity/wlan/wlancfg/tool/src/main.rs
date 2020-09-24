// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fuchsia_async as fasync;
use structopt::StructOpt;

mod opts;
use crate::opts::*;

mod policy;
use crate::policy::*;

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    println!("{:?}", opt);

    let mut exec = fasync::Executor::new().context("error creating event loop")?;

    let fut = async {
        match opt {
            Opt::Client(cmd) => do_policy_client_cmd(cmd).await,
            Opt::AccessPoint(cmd) => do_policy_ap_cmd(cmd).await,
            Opt::Deprecated(cmd) => do_deprecated_cmd(cmd).await,
        }
    };
    exec.run_singlethreaded(fut)
}

async fn do_policy_client_cmd(cmd: opts::PolicyClientCmd) -> Result<(), Error> {
    match cmd {
        opts::PolicyClientCmd::Connect(network_id) => {
            let (client_controller, updates_server_end) = get_client_controller()?;
            handle_connect(client_controller, updates_server_end, network_id).await?;
        }
        opts::PolicyClientCmd::GetSavedNetworks => {
            let (client_controller, _) = get_client_controller()?;
            let saved_networks = handle_get_saved_networks(client_controller).await?;
            print_saved_networks(saved_networks)?;
        }
        opts::PolicyClientCmd::Listen => {
            let update_stream = get_listener_stream()?;
            handle_listen(update_stream).await?;
        }
        opts::PolicyClientCmd::RemoveNetwork(network_config) => {
            let (client_controller, _) = get_client_controller()?;
            handle_remove_network(client_controller, network_config).await?;
        }
        opts::PolicyClientCmd::SaveNetwork(network_config) => {
            let (client_controller, _) = get_client_controller()?;
            handle_save_network(client_controller, network_config).await?;
        }
        opts::PolicyClientCmd::ScanForNetworks => {
            let (client_controller, _) = get_client_controller()?;
            let scan_results = handle_scan(client_controller).await?;
            print_scan_results(scan_results)?;
        }
        opts::PolicyClientCmd::StartClientConnections => {
            let (client_controller, _) = get_client_controller()?;
            handle_start_client_connections(client_controller).await?;
        }
        opts::PolicyClientCmd::StopClientConnections => {
            let (client_controller, _) = get_client_controller()?;
            handle_stop_client_connections(client_controller).await?;
        }
    }
    Ok(())
}

async fn do_policy_ap_cmd(cmd: opts::PolicyAccessPointCmd) -> Result<(), Error> {
    match cmd {
        opts::PolicyAccessPointCmd::Start(network_config) => {
            let (ap_controller, updates_server_end) = get_ap_controller()?;
            handle_start_ap(ap_controller, updates_server_end, network_config).await?;
        }
        opts::PolicyAccessPointCmd::Stop(network_config) => {
            let (ap_controller, _) = get_ap_controller()?;
            handle_stop_ap(ap_controller, network_config).await?;
        }
        opts::PolicyAccessPointCmd::StopAllAccessPoints => {
            let (ap_controller, _) = get_ap_controller()?;
            handle_stop_all_aps(ap_controller).await?;
        }
        opts::PolicyAccessPointCmd::Listen => {
            let update_stream = get_ap_listener_stream()?;
            handle_ap_listen(update_stream).await?
        }
    }
    Ok(())
}

async fn do_deprecated_cmd(cmd: opts::DeprecatedConfiguratorCmd) -> Result<(), Error> {
    match cmd {
        opts::DeprecatedConfiguratorCmd::SuggestAccessPointMacAddress { mac } => {
            let configurator = get_deprecated_configurator()?;
            handle_suggest_ap_mac(configurator, mac).await?;
        }
    }
    Ok(())
}
