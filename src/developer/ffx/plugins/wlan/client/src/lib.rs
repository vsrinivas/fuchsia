// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, donut_lib, ffx_core::ffx_plugin, ffx_wlan_client_args as arg_types,
    ffx_wlan_common, fidl_fuchsia_wlan_policy as wlan_policy,
};

#[ffx_plugin(
    wlan_policy::ClientProviderProxy = "core/wlancfg:expose:fuchsia.wlan.policy.ClientProvider",
    wlan_policy::ClientListenerProxy = "core/wlancfg:expose:fuchsia.wlan.policy.ClientListener"
)]
pub async fn handle_client_command(
    client_provider: wlan_policy::ClientProviderProxy,
    client_listener: wlan_policy::ClientListenerProxy,
    cmd: arg_types::ClientCommand,
) -> Result<(), Error> {
    let (client_controller, _) = ffx_wlan_common::get_client_controller(client_provider).await?;
    let listener_stream = ffx_wlan_common::get_client_listener_stream(client_listener)?;

    match cmd.subcommand {
        arg_types::ClientSubCommand::BatchConfig(batch_cmd) => match batch_cmd.subcommand {
            arg_types::BatchConfigSubCommand::Dump(arg_types::Dump {}) => {
                let saved_networks =
                    donut_lib::handle_get_saved_networks(&client_controller).await?;
                donut_lib::print_serialized_saved_networks(saved_networks)
            }
            arg_types::BatchConfigSubCommand::Restore(arg_types::Restore { serialized_config }) => {
                donut_lib::restore_serialized_config(client_controller, serialized_config).await
            }
        },
        arg_types::ClientSubCommand::Connect(connect_args) => {
            let security = connect_args.security_type.map(|s| s.into());
            donut_lib::handle_connect(
                client_controller,
                listener_stream,
                connect_args.ssid,
                security,
            )
            .await
        }
        arg_types::ClientSubCommand::List(arg_types::ListSavedNetworks {}) => {
            let saved_networks = donut_lib::handle_get_saved_networks(&client_controller).await?;
            donut_lib::print_saved_networks(saved_networks)
        }
        arg_types::ClientSubCommand::Listen(arg_types::Listen {}) => {
            donut_lib::handle_listen(listener_stream).await
        }
        arg_types::ClientSubCommand::RemoveNetwork(config) => {
            let network_config = wlan_policy::NetworkConfig::from(config);
            donut_lib::handle_remove_network(client_controller, network_config).await
        }
        arg_types::ClientSubCommand::SaveNetwork(config) => {
            let network_config = wlan_policy::NetworkConfig::from(config);
            donut_lib::handle_save_network(client_controller, network_config).await
        }
        arg_types::ClientSubCommand::Scan(arg_types::Scan {}) => {
            let scan_results = donut_lib::handle_scan(client_controller).await?;
            donut_lib::print_scan_results(scan_results)
        }
        arg_types::ClientSubCommand::Start(arg_types::StartClientConnections {}) => {
            donut_lib::handle_start_client_connections(client_controller).await
        }
        arg_types::ClientSubCommand::Stop(arg_types::StopClientConnections {}) => {
            donut_lib::handle_stop_client_connections(client_controller).await
        }
    }
}
