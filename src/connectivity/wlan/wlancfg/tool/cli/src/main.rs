// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    donut_lib::*,
    fidl::endpoints::{create_endpoints, create_proxy},
    fidl_fuchsia_wlan_policy as wlan_policy,
    fidl_fuchsia_wlan_product_deprecatedconfiguration as wlan_deprecated, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    structopt::StructOpt,
};

mod opts;
use crate::opts::*;

/// Communicates with the client policy provider to get the components required to get a client
/// controller.
pub async fn get_client_controller(
) -> Result<(wlan_policy::ClientControllerProxy, wlan_policy::ClientStateUpdatesRequestStream), Error>
{
    let policy_provider = connect_to_protocol::<wlan_policy::ClientProviderMarker>()?;
    let (client_controller, server_end) =
        create_proxy::<wlan_policy::ClientControllerMarker>().unwrap();
    let (update_client_end, update_server_end) =
        create_endpoints::<wlan_policy::ClientStateUpdatesMarker>().unwrap();
    let () = policy_provider.get_controller(server_end, update_client_end)?;
    let update_stream = update_server_end.into_stream()?;

    Ok((client_controller, update_stream))
}

/// Communicates with the AP policy provider to get the components required to get an AP
/// controller.
pub fn get_ap_controller() -> Result<
    (wlan_policy::AccessPointControllerProxy, wlan_policy::AccessPointStateUpdatesRequestStream),
    Error,
> {
    let policy_provider = connect_to_protocol::<wlan_policy::AccessPointProviderMarker>()?;
    let (ap_controller, server_end) =
        create_proxy::<wlan_policy::AccessPointControllerMarker>().unwrap();
    let (update_client_end, update_server_end) =
        create_endpoints::<wlan_policy::AccessPointStateUpdatesMarker>().unwrap();
    let () = policy_provider.get_controller(server_end, update_client_end)?;
    let update_stream = update_server_end.into_stream()?;

    Ok((ap_controller, update_stream))
}

/// Communicates with the client listener service to get a stream of client state updates.
pub fn get_listener_stream() -> Result<wlan_policy::ClientStateUpdatesRequestStream, Error> {
    let listener = connect_to_protocol::<wlan_policy::ClientListenerMarker>()?;
    let (client_end, server_end) =
        create_endpoints::<wlan_policy::ClientStateUpdatesMarker>().unwrap();
    listener.get_listener(client_end)?;
    let server_stream = server_end.into_stream()?;
    Ok(server_stream)
}

/// Communicates with the AP listener service to get a stream of AP state updates.
pub fn get_ap_listener_stream() -> Result<wlan_policy::AccessPointStateUpdatesRequestStream, Error>
{
    let listener = connect_to_protocol::<wlan_policy::AccessPointListenerMarker>()?;
    let (client_end, server_end) =
        create_endpoints::<wlan_policy::AccessPointStateUpdatesMarker>().unwrap();
    listener.get_listener(client_end)?;
    let server_stream = server_end.into_stream()?;
    Ok(server_stream)
}

/// Creates a channel to interact with the DeprecatedConfigurator service.
pub fn get_deprecated_configurator() -> Result<wlan_deprecated::DeprecatedConfiguratorProxy, Error>
{
    let configurator = connect_to_protocol::<wlan_deprecated::DeprecatedConfiguratorMarker>()?;
    Ok(configurator)
}

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();

    let mut exec = fasync::LocalExecutor::new().context("error creating event loop")?;

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
        opts::PolicyClientCmd::Connect(connect_args) => {
            let (client_controller, updates_server_end) = get_client_controller().await?;
            let security = connect_args.security_type.map(|s| s.into());
            handle_connect(client_controller, updates_server_end, connect_args.ssid, security)
                .await?;
        }
        opts::PolicyClientCmd::GetSavedNetworks => {
            let (client_controller, _) = get_client_controller().await?;
            let saved_networks = handle_get_saved_networks(&client_controller).await?;
            print_saved_networks(saved_networks)?;
        }
        opts::PolicyClientCmd::Listen => {
            let update_stream = get_listener_stream()?;
            handle_listen(update_stream).await?;
        }
        opts::PolicyClientCmd::RemoveNetwork(network_config) => {
            let (client_controller, _) = get_client_controller().await?;
            let network_config = wlan_policy::NetworkConfig::from(network_config);
            handle_remove_network(client_controller, network_config).await?;
        }
        opts::PolicyClientCmd::SaveNetwork(network_config) => {
            let (client_controller, _) = get_client_controller().await?;
            let network_config = wlan_policy::NetworkConfig::from(network_config);
            handle_save_network(client_controller, network_config).await?;
        }
        opts::PolicyClientCmd::ScanForNetworks => {
            let (client_controller, _) = get_client_controller().await?;
            let scan_results = handle_scan(client_controller).await?;
            print_scan_results(scan_results)?;
        }
        opts::PolicyClientCmd::StartClientConnections => {
            let (client_controller, _) = get_client_controller().await?;
            handle_start_client_connections(client_controller).await?;
        }
        opts::PolicyClientCmd::StopClientConnections => {
            let (client_controller, _) = get_client_controller().await?;
            handle_stop_client_connections(client_controller).await?;
        }
        opts::PolicyClientCmd::DumpConfig => {
            let (client_controller, _) = get_client_controller().await?;
            let saved_networks = handle_get_saved_networks(&client_controller).await?;
            print_serialized_saved_networks(saved_networks)?;
        }
        opts::PolicyClientCmd::RestoreConfig { serialized_config } => {
            let (client_controller, _) = get_client_controller().await?;
            restore_serialized_config(client_controller, serialized_config).await?;
        }
    }
    Ok(())
}

async fn do_policy_ap_cmd(cmd: opts::PolicyAccessPointCmd) -> Result<(), Error> {
    match cmd {
        opts::PolicyAccessPointCmd::Start(network_config) => {
            let (ap_controller, updates_server_end) = get_ap_controller()?;
            let network_config = wlan_policy::NetworkConfig::from(network_config);
            handle_start_ap(ap_controller, updates_server_end, network_config).await?;
        }
        opts::PolicyAccessPointCmd::Stop(network_config) => {
            let (ap_controller, _) = get_ap_controller()?;
            let network_config = wlan_policy::NetworkConfig::from(network_config);
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
