// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    eui48::MacAddress,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_wlan_common as fidl_wlan_common, fidl_fuchsia_wlan_policy as wlan_policy,
    fidl_fuchsia_wlan_product_deprecatedconfiguration as wlan_deprecated,
    futures::{future::BoxFuture, TryStreamExt},
};

pub mod serialize;

// String formatting, printing, and general boilerplate helpers.

/// Returns the SSID and security type of a network identifier as strings.
fn extract_network_id(
    network_id: Option<fidl_fuchsia_wlan_policy::NetworkIdentifier>,
) -> Result<(String, String), Error> {
    match network_id {
        Some(id) => {
            let ssid = std::string::String::from_utf8(id.ssid).unwrap();
            let security_type = match id.type_ {
                fidl_fuchsia_wlan_policy::SecurityType::None => "",
                fidl_fuchsia_wlan_policy::SecurityType::Wep => "wep",
                fidl_fuchsia_wlan_policy::SecurityType::Wpa => "wpa",
                fidl_fuchsia_wlan_policy::SecurityType::Wpa2 => "wpa2",
                fidl_fuchsia_wlan_policy::SecurityType::Wpa3 => "wpa3",
            };
            return Ok((ssid, security_type.to_string()));
        }
        None => return Ok(("".to_string(), "".to_string())),
    };
}

/// Returns a BSS's BSSID as a string.
fn extract_bss_details(bss: wlan_policy::Bss) -> (String, i8, u32) {
    let bssid = match bss.bssid {
        Some(bssid) => hex::encode(bssid),
        None => "".to_string(),
    };
    let rssi = match bss.rssi {
        Some(rssi) => rssi,
        None => 0,
    };
    let frequency = match bss.frequency {
        Some(frequency) => frequency,
        None => 0,
    };

    (bssid, rssi, frequency)
}

/// Returns WLAN compatilibity information as a string.
fn extract_compatibility(compatibility: Option<wlan_policy::Compatibility>) -> String {
    if compatibility.is_some() {
        match compatibility.unwrap() {
            wlan_policy::Compatibility::Supported => return "supported".to_string(),
            wlan_policy::Compatibility::DisallowedInsecure => {
                return "disallowed, insecure".to_string();
            }
            wlan_policy::Compatibility::DisallowedNotSupported => {
                return "disallowed, not supported".to_string();
            }
        }
    }

    return "compatilibity unknown".to_string();
}

/// Iterates through a vector of network configurations and prints their contents.
pub fn print_saved_networks(saved_networks: Vec<wlan_policy::NetworkConfig>) -> Result<(), Error> {
    for config in saved_networks {
        let (ssid, security_type) = extract_network_id(config.id)?;
        let password = match config.credential {
            Some(credential) => match credential {
                wlan_policy::Credential::None(wlan_policy::Empty) => String::from(""),
                wlan_policy::Credential::Password(bytes) => {
                    let password = std::string::String::from_utf8(bytes);
                    password.unwrap()
                }
                wlan_policy::Credential::Psk(bytes) => {
                    // PSK is stored as bytes but is displayed as hex to prevent UTF-8 errors
                    hex::encode(bytes)
                }
                _ => return Err(format_err!("unknown credential variant detected")),
            },
            None => String::from(""),
        };
        println!("{:32} | {:4} | {}", ssid, security_type, password);
    }
    Ok(())
}

/// Prints a serialized version of saved networks
pub fn print_serialized_saved_networks(
    saved_networks: Vec<wlan_policy::NetworkConfig>,
) -> Result<(), Error> {
    let serialized = serialize::serialize_saved_networks(saved_networks)?;
    println!("{}", serialized);
    Ok(())
}

/// Deserializes the output of serialize_saved_networks and saves them
pub async fn restore_serialized_config(
    client_controller: wlan_policy::ClientControllerProxy,
    serialized_config: String,
) -> Result<(), Error> {
    let network_configs = serialize::deserialize_saved_networks(serialized_config)?;
    for network in network_configs {
        save_network(client_controller.clone(), network).await?;
    }
    Ok(())
}

/// Iterates through a vector of scan results and prints each one.
pub fn print_scan_results(scan_results: Vec<wlan_policy::ScanResult>) -> Result<(), Error> {
    for network in scan_results {
        let (ssid, security_type) = extract_network_id(network.id)?;
        let compatibility = extract_compatibility(network.compatibility);
        println!("{:32} | {:3} ({})", ssid, security_type, compatibility);

        if network.entries.is_some() {
            for entry in network.entries.unwrap() {
                let (bssid, rssi, frequency) = extract_bss_details(entry);
                println!("\t0x{:12} | {:3} | {:5}", bssid, rssi, frequency);
            }
        }
    }
    Ok(())
}

/// Parses the return value from policy layer FIDL operations and prints a human-readable
/// representation of the result.
fn handle_request_status(status: fidl_wlan_common::RequestStatus) -> Result<(), Error> {
    match status {
        fidl_wlan_common::RequestStatus::Acknowledged => Ok(()),
        fidl_wlan_common::RequestStatus::RejectedNotSupported => {
            Err(format_err!("request failed: not supported"))
        }
        fidl_wlan_common::RequestStatus::RejectedIncompatibleMode => {
            Err(format_err!("request failed: incompatible mode"))
        }
        fidl_wlan_common::RequestStatus::RejectedAlreadyInUse => {
            Err(format_err!("request failed: already in use"))
        }
        fidl_wlan_common::RequestStatus::RejectedDuplicateRequest => {
            Err(format_err!("request failed: duplicate request."))
        }
    }
}

/// When a client or AP controller is created, the policy layer may close the serving end with an
/// epitaph if another component already holds a controller.  This macro wraps proxy API calls
/// and provides context to the caller.
async fn run_proxy_command<'a, T>(fut: BoxFuture<'a, Result<T, fidl::Error>>) -> Result<T, Error> {
    fut.await.map_err(|e| {
        match e {
            fidl::Error::ClientChannelClosed{ .. } => format_err!(
                "Failed to obtain a WLAN policy controller. Your command was not executed.\n\n\
                Help: Only one component may hold a policy controller at once. You can try killing\n\
                other holders with one of the following:\n\
                * ffx component destroy /core/session-manager/session:session\n\
                * [older product versions only] killall basemgr.cmx\n"
            ),
            e => format_err!("{}", e)
        }
    })
}

// Policy client helper functions

/// Issues a connect call to the client policy layer and waits for the connection process to
/// complete.
pub async fn handle_connect(
    client_controller: wlan_policy::ClientControllerProxy,
    mut server_stream: wlan_policy::ClientStateUpdatesRequestStream,
    mut network_id: wlan_policy::NetworkIdentifier,
) -> Result<(), Error> {
    let result = run_proxy_command(Box::pin(client_controller.connect(&mut network_id))).await?;
    handle_request_status(result)?;

    while let Some(update_request) = server_stream.try_next().await? {
        let update = update_request.into_on_client_state_update();
        let (update, responder) = match update {
            Some((update, responder)) => (update, responder),
            None => return Err(format_err!("Client provider produced invalid update.")),
        };
        let _ = responder.send();

        match update.state {
            Some(state) => {
                if state == wlan_policy::WlanClientState::ConnectionsDisabled {
                    return Err(format_err!("Connections disabled while trying to conncet."));
                }
            }
            None => continue,
        }

        let networks = match update.networks {
            Some(networks) => networks,
            None => continue,
        };

        for net_state in networks {
            if net_state.id.is_none() || net_state.state.is_none() {
                continue;
            }
            if net_state.id.unwrap().ssid == network_id.ssid {
                match net_state.state.unwrap() {
                    wlan_policy::ConnectionState::Failed => {
                        return Err(format_err!(
                            "Failed to connect with reason {:?}",
                            net_state.status
                        ));
                    }
                    wlan_policy::ConnectionState::Disconnected => {
                        return Err(format_err!("Disconnect with reason {:?}", net_state.status));
                    }
                    wlan_policy::ConnectionState::Connecting => continue,
                    wlan_policy::ConnectionState::Connected => return Ok(()),
                }
            }
        }
    }
    Err(format_err!("Status stream terminated before connection could be verified."))
}

/// Issues a call to the client policy layer to get saved networks and prints all saved network
/// configurations.
pub async fn handle_get_saved_networks(
    client_controller: wlan_policy::ClientControllerProxy,
) -> Result<Vec<wlan_policy::NetworkConfig>, Error> {
    let (client_proxy, server_end) =
        create_proxy::<wlan_policy::NetworkConfigIteratorMarker>().unwrap();
    let fut = async { client_controller.get_saved_networks(server_end) };
    run_proxy_command(Box::pin(fut)).await?;

    let mut saved_networks = Vec::new();
    loop {
        let mut new_configs = run_proxy_command(Box::pin(client_proxy.get_next())).await?;
        if new_configs.is_empty() {
            break;
        }
        saved_networks.append(&mut new_configs);
    }
    Ok(saved_networks)
}

/// Listens for client state updates and prints each update that is received. Updates are printed
/// in the following format:
///
/// Update:
///     <SSIDA>:                  <state> - <status_if_any>,
///     <SSIDB_LONGER>:           <state> - <status_if_any>,
pub async fn handle_listen(
    mut server_stream: wlan_policy::ClientStateUpdatesRequestStream,
) -> Result<(), Error> {
    let mut last_known_connection_state = None;
    while let Some(update_request) = server_stream.try_next().await? {
        let update = update_request.into_on_client_state_update();
        let (update, responder) = match update {
            Some((update, responder)) => (update, responder),
            None => return Err(format_err!("Client provider produced invalid update.")),
        };
        let _ = responder.send();

        match update.state {
            Some(state) => {
                if last_known_connection_state != Some(state) {
                    last_known_connection_state = Some(state);
                    match state {
                        wlan_policy::WlanClientState::ConnectionsEnabled => {
                            println!("Client connections are enabled");
                        }
                        wlan_policy::WlanClientState::ConnectionsDisabled => {
                            println!("Client connections are disabled");
                        }
                    }
                };
            }
            None => {
                println!("Unexpected client connection state 'None'");
            }
        }

        let networks = match update.networks {
            Some(networks) => networks,
            None => continue,
        };

        let mut updates = vec![];
        // Create update string for each network. Pad the SSID so the updates align
        for net_state in networks {
            if net_state.id.is_none() || net_state.state.is_none() {
                continue;
            }
            let id = net_state.id.unwrap();
            let ssid = std::str::from_utf8(&id.ssid).unwrap();
            match net_state.state.unwrap() {
                wlan_policy::ConnectionState::Failed => {
                    updates.push(format!(
                        "\t{:32} connection failed - {:?}",
                        format!("{}:", ssid),
                        net_state.status
                    ));
                }
                wlan_policy::ConnectionState::Disconnected => {
                    updates.push(format!(
                        "\t{:32} connection disconnected - {:?}",
                        format!("{}:", ssid),
                        net_state.status
                    ));
                }
                wlan_policy::ConnectionState::Connecting => {
                    updates.push(format!("\t{:32} connecting", format!("{}:", ssid)));
                }
                wlan_policy::ConnectionState::Connected => {
                    updates.push(format!("\t{:32} connected", format!("{}:", ssid)))
                }
            }
        }

        // Sort updates by SSID
        updates.sort_by_key(|s| s.to_lowercase());
        println!("Update:");
        for update in updates {
            println!("{}", update);
        }
    }
    Ok(())
}

/// Communicates with the client policy layer to remove a network.
pub async fn handle_remove_network(
    client_controller: wlan_policy::ClientControllerProxy,
    config: wlan_policy::NetworkConfig,
) -> Result<(), Error> {
    let id = config.id.clone();
    let (ssid, _) = extract_network_id(id).expect("Failed to convert network ssid.");
    run_proxy_command(Box::pin(client_controller.remove_network(config)))
        .await?
        .map_err(|e| format_err!("failed to remove network with {:?}", e))?;
    println!("Successfully removed network '{}'", ssid);
    Ok(())
}

/// Communicates with the client policy layer to save a network configuration.
pub async fn handle_save_network(
    client_controller: wlan_policy::ClientControllerProxy,
    config: wlan_policy::NetworkConfig,
) -> Result<(), Error> {
    save_network(client_controller, config).await
}

async fn save_network(
    client_controller: wlan_policy::ClientControllerProxy,
    network_config: wlan_policy::NetworkConfig,
) -> Result<(), Error> {
    run_proxy_command(Box::pin(client_controller.save_network(network_config.clone())))
        .await?
        .map_err(|e| format_err!("failed to save network with {:?}", e))?;
    println!(
        "Successfully saved network '{}'",
        std::str::from_utf8(&network_config.id.unwrap().ssid).unwrap()
    );
    Ok(())
}

/// Issues a scan request to the client policy layer.
pub async fn handle_scan(
    client_controller: wlan_policy::ClientControllerProxy,
) -> Result<Vec<wlan_policy::ScanResult>, Error> {
    let (client_proxy, server_end) =
        create_proxy::<wlan_policy::ScanResultIteratorMarker>().unwrap();
    let fut = async { client_controller.scan_for_networks(server_end) };
    run_proxy_command(Box::pin(fut)).await?;

    let mut scanned_networks = Vec::<wlan_policy::ScanResult>::new();
    loop {
        match run_proxy_command(Box::pin(client_proxy.get_next())).await? {
            Ok(mut new_networks) => {
                if new_networks.is_empty() {
                    break;
                }
                scanned_networks.append(&mut new_networks);
            }
            Err(e) => return Err(format_err!("Scan failure error: {:?}", e)),
        }
    }

    Ok(scanned_networks)
}

/// Requests that the policy layer start client connections.
pub async fn handle_start_client_connections(
    client_controller: wlan_policy::ClientControllerProxy,
) -> Result<(), Error> {
    let status = run_proxy_command(Box::pin(client_controller.start_client_connections())).await?;
    return handle_request_status(status);
}

/// Asks the client policy layer to stop client connections.
pub async fn handle_stop_client_connections(
    client_controller: wlan_policy::ClientControllerProxy,
) -> Result<(), Error> {
    let status = run_proxy_command(Box::pin(client_controller.stop_client_connections())).await?;
    return handle_request_status(status);
}

/// Asks the policy layer to start an AP with the user's specified network configuration.
pub async fn handle_start_ap(
    ap_controller: wlan_policy::AccessPointControllerProxy,
    mut server_stream: wlan_policy::AccessPointStateUpdatesRequestStream,
    config: wlan_policy::NetworkConfig,
) -> Result<(), Error> {
    let connectivity_mode = wlan_policy::ConnectivityMode::Unrestricted;
    let operating_band = wlan_policy::OperatingBand::Any;
    let result = run_proxy_command(Box::pin(ap_controller.start_access_point(
        config,
        connectivity_mode,
        operating_band,
    )))
    .await?;
    handle_request_status(result)?;

    // Listen for state updates until the service indicates that there is an active AP.
    while let Some(update_request) = server_stream.try_next().await? {
        let update = update_request.into_on_access_point_state_update();
        let (updates, responder) = match update {
            Some((update, responder)) => (update, responder),
            None => return Err(format_err!("AP provider produced invalid update.")),
        };
        let _ = responder.send();

        for update in updates {
            match update.state {
                Some(state) => match state {
                    wlan_policy::OperatingState::Failed => {
                        return Err(format_err!("Failed to start AP."));
                    }
                    wlan_policy::OperatingState::Starting => {
                        println!("AP is starting.");
                        continue;
                    }
                    wlan_policy::OperatingState::Active => return Ok(()),
                },
                None => continue,
            }
        }
    }
    Err(format_err!("Status stream terminated before AP start could be verified."))
}

/// Requests that the policy layer stop the AP associated with the given network configuration.
pub async fn handle_stop_ap(
    ap_controller: wlan_policy::AccessPointControllerProxy,
    config: wlan_policy::NetworkConfig,
) -> Result<(), Error> {
    let result = run_proxy_command(Box::pin(ap_controller.stop_access_point(config))).await?;
    handle_request_status(result)
}

/// Requests that the policy layer stop all AP interfaces.
pub async fn handle_stop_all_aps(
    ap_controller: wlan_policy::AccessPointControllerProxy,
) -> Result<(), Error> {
    let fut = async { ap_controller.stop_all_access_points() };
    run_proxy_command(Box::pin(fut)).await?;
    Ok(())
}

/// Listens for AP state updates and prints each update that is received.
pub async fn handle_ap_listen(
    mut server_stream: wlan_policy::AccessPointStateUpdatesRequestStream,
) -> Result<(), Error> {
    println!(
        "{:32} | {:4} | {:8} | {:12} | {:6} | {:4} | {:7}",
        "SSID", "Type", "State", "Mode", "Band", "Freq", "#Clients"
    );

    while let Some(update_request) = server_stream.try_next().await? {
        let updates = update_request.into_on_access_point_state_update();
        let (updates, responder) = match updates {
            Some((update, responder)) => (update, responder),
            None => return Err(format_err!("AP provider produced invalid update.")),
        };
        let _ = responder.send();

        for update in updates {
            let (ssid, security_type) = match update.id {
                Some(network_id) => {
                    let ssid = network_id.ssid.clone();
                    let ssid = String::from_utf8(ssid)?;
                    let security_type = match network_id.type_ {
                        wlan_policy::SecurityType::None => "none",
                        wlan_policy::SecurityType::Wep => "wep",
                        wlan_policy::SecurityType::Wpa => "wpa",
                        wlan_policy::SecurityType::Wpa2 => "wpa2",
                        wlan_policy::SecurityType::Wpa3 => "wpa3",
                    };
                    (ssid, security_type.to_string())
                }
                None => ("".to_string(), "".to_string()),
            };
            let state = match update.state {
                Some(state) => match state {
                    wlan_policy::OperatingState::Failed => "failed",
                    wlan_policy::OperatingState::Starting => "starting",
                    wlan_policy::OperatingState::Active => "active",
                },
                None => "",
            };
            let mode = match update.mode {
                Some(mode) => match mode {
                    wlan_policy::ConnectivityMode::LocalOnly => "local only",
                    wlan_policy::ConnectivityMode::Unrestricted => "unrestricted",
                },
                None => "",
            };
            let band = match update.band {
                Some(band) => match band {
                    wlan_policy::OperatingBand::Any => "any",
                    wlan_policy::OperatingBand::Only24Ghz => "2.4Ghz",
                    wlan_policy::OperatingBand::Only5Ghz => "5Ghz",
                },
                None => "",
            };
            let frequency = match update.frequency {
                Some(frequency) => frequency.to_string(),
                None => "".to_string(),
            };
            let client_count = match update.clients {
                Some(connected_clients) => match connected_clients.count {
                    Some(count) => count.to_string(),
                    None => "".to_string(),
                },
                None => "".to_string(),
            };

            println!(
                "{:32} | {:4} | {:8} | {:12} | {:6} | {:4} | {:7}",
                ssid, security_type, state, mode, band, frequency, client_count
            );
        }
    }
    Ok(())
}

pub async fn handle_suggest_ap_mac(
    configurator: wlan_deprecated::DeprecatedConfiguratorProxy,
    mac: MacAddress,
) -> Result<(), Error> {
    let mut mac = fidl_fuchsia_net::MacAddress { octets: mac.to_array() };
    let result = configurator.suggest_access_point_mac_address(&mut mac).await?;
    result.map_err(|e| format_err!("suggesting MAC failed: {:?}", e))
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints,
        fuchsia_async::TestExecutor,
        fuchsia_zircon_status as zx_status,
        futures::{stream::StreamExt, task::Poll},
        pin_utils::pin_mut,
        wlan_common::assert_variant,
    };

    static TEST_SSID: &str = "test_ssid";

    struct ClientTestValues {
        client_proxy: wlan_policy::ClientControllerProxy,
        client_stream: wlan_policy::ClientControllerRequestStream,
        update_proxy: wlan_policy::ClientStateUpdatesProxy,
        update_stream: wlan_policy::ClientStateUpdatesRequestStream,
    }

    fn client_test_setup() -> ClientTestValues {
        let (client_proxy, client_stream) =
            endpoints::create_proxy_and_stream::<wlan_policy::ClientControllerMarker>()
                .expect("failed to create ClientController proxy");

        let (listener_proxy, listener_stream) =
            endpoints::create_proxy_and_stream::<wlan_policy::ClientStateUpdatesMarker>()
                .expect("failed to create ClientController proxy");

        ClientTestValues {
            client_proxy: client_proxy,
            client_stream: client_stream,
            update_proxy: listener_proxy,
            update_stream: listener_stream,
        }
    }

    /// Allows callers to respond to StartClientConnections, StopClientConnections, and Connect
    /// calls with the RequestStatus response of their choice.
    fn send_client_request_status(
        exec: &mut TestExecutor,
        server: &mut wlan_policy::ClientControllerRequestStream,
        response: fidl_wlan_common::RequestStatus,
    ) {
        let poll = exec.run_until_stalled(&mut server.next());
        let request = match poll {
            Poll::Ready(poll_ready) => poll_ready
                .expect("poll ready result is None")
                .expect("poll ready result is an Error"),
            Poll::Pending => panic!("no ClientController request available"),
        };

        let result = match request {
            wlan_policy::ClientControllerRequest::StartClientConnections { responder } => {
                responder.send(response)
            }
            wlan_policy::ClientControllerRequest::StopClientConnections { responder } => {
                responder.send(response)
            }
            wlan_policy::ClientControllerRequest::Connect { responder, .. } => {
                responder.send(response)
            }
            _ => panic!("expecting a request that expects a RequestStatus"),
        };

        if result.is_err() {
            panic!("could not send request status");
        }
    }

    /// Creates a ClientStateSummary to provide as an update to listeners.
    fn create_client_state_summary(
        ssid: &str,
        state: wlan_policy::ConnectionState,
    ) -> wlan_policy::ClientStateSummary {
        let network_state = wlan_policy::NetworkState {
            id: Some(create_network_id(ssid)),
            state: Some(state),
            status: None,
            ..wlan_policy::NetworkState::EMPTY
        };
        wlan_policy::ClientStateSummary {
            state: Some(wlan_policy::WlanClientState::ConnectionsEnabled),
            networks: Some(vec![network_state]),
            ..wlan_policy::ClientStateSummary::EMPTY
        }
    }

    /// Creates an AccessPointStateSummary to provide as an update to listeners.
    fn create_ap_state_summary(
        state: wlan_policy::OperatingState,
    ) -> wlan_policy::AccessPointState {
        wlan_policy::AccessPointState {
            id: None,
            state: Some(state),
            mode: None,
            band: None,
            frequency: None,
            clients: None,
            ..wlan_policy::AccessPointState::EMPTY
        }
    }

    /// Allows callers to send a response to SaveNetwork and RemoveNetwork calls.
    fn send_network_config_response(
        exec: &mut TestExecutor,
        mut server: wlan_policy::ClientControllerRequestStream,
        success: bool,
    ) {
        let poll = exec.run_until_stalled(&mut server.next());
        let request = match poll {
            Poll::Ready(poll_ready) => poll_ready
                .expect("poll ready result is None")
                .expect("poll ready result is an Error"),
            Poll::Pending => panic!("no ClientController request available"),
        };

        let result = match request {
            wlan_policy::ClientControllerRequest::SaveNetwork { config: _, responder } => {
                if success {
                    responder.send(&mut Ok(()))
                } else {
                    responder.send(&mut Err(wlan_policy::NetworkConfigChangeError::GeneralError))
                }
            }
            wlan_policy::ClientControllerRequest::RemoveNetwork { config: _, responder } => {
                if success {
                    responder.send(&mut Ok(()))
                } else {
                    responder.send(&mut Err(wlan_policy::NetworkConfigChangeError::GeneralError))
                }
            }
            _ => panic!("expecting a request that optionally receives a NetworkConfigChangeError"),
        };

        if result.is_err() {
            panic!("could not send network config response");
        }
    }

    struct ApTestValues {
        ap_proxy: wlan_policy::AccessPointControllerProxy,
        ap_stream: wlan_policy::AccessPointControllerRequestStream,
        update_proxy: wlan_policy::AccessPointStateUpdatesProxy,
        update_stream: wlan_policy::AccessPointStateUpdatesRequestStream,
    }

    fn ap_test_setup() -> ApTestValues {
        let (ap_proxy, ap_stream) =
            endpoints::create_proxy_and_stream::<wlan_policy::AccessPointControllerMarker>()
                .expect("failed to create AccessPointController proxy");

        let (listener_proxy, listener_stream) =
            endpoints::create_proxy_and_stream::<wlan_policy::AccessPointStateUpdatesMarker>()
                .expect("failed to create AccessPointController proxy");

        ApTestValues {
            ap_proxy,
            ap_stream,
            update_proxy: listener_proxy,
            update_stream: listener_stream,
        }
    }

    /// Allows callers to respond to StartAccessPoint and StopAccessPoint
    /// calls with the RequestStatus response of their choice.
    fn send_ap_request_status(
        exec: &mut TestExecutor,
        server: &mut wlan_policy::AccessPointControllerRequestStream,
        response: fidl_wlan_common::RequestStatus,
    ) {
        let poll = exec.run_until_stalled(&mut server.next());
        let request = match poll {
            Poll::Ready(poll_ready) => poll_ready
                .expect("poll ready result is None")
                .expect("poll ready result is an Error"),
            Poll::Pending => panic!("no AccessPointController request available"),
        };

        let result = match request {
            wlan_policy::AccessPointControllerRequest::StartAccessPoint { responder, .. } => {
                responder.send(response)
            }
            wlan_policy::AccessPointControllerRequest::StopAccessPoint { config: _, responder } => {
                responder.send(response)
            }
            _ => panic!("expecting a request that expects a RequestStatus"),
        };

        if result.is_err() {
            panic!("could not send request status");
        }
    }

    /// Creates a NetworkIdentifier for use in tests.
    fn create_network_id(ssid: &str) -> fidl_fuchsia_wlan_policy::NetworkIdentifier {
        wlan_policy::NetworkIdentifier {
            ssid: ssid.as_bytes().to_vec(),
            type_: wlan_policy::SecurityType::Wpa2,
        }
    }

    /// Creates a NetworkConfig for use in tests.
    fn create_network_config(ssid: &str) -> fidl_fuchsia_wlan_policy::NetworkConfig {
        wlan_policy::NetworkConfig {
            id: Some(create_network_id(ssid)),
            credential: None,
            ..wlan_policy::NetworkConfig::EMPTY
        }
    }

    /// Create a scan result to be sent as a response to a scan request.
    fn create_scan_result(ssid: &str) -> wlan_policy::ScanResult {
        wlan_policy::ScanResult {
            id: Some(create_network_id(ssid)),
            entries: None,
            compatibility: None,
            ..wlan_policy::ScanResult::EMPTY
        }
    }

    /// Respond to a ScanForNetworks request and provide an iterator so that tests can inject scan
    /// results.
    fn get_scan_result_iterator(
        exec: &mut TestExecutor,
        mut server: wlan_policy::ClientControllerRequestStream,
    ) -> wlan_policy::ScanResultIteratorRequestStream {
        let poll = exec.run_until_stalled(&mut server.next());
        let request = match poll {
            Poll::Ready(poll_ready) => poll_ready
                .expect("poll ready result is None")
                .expect("poll ready result is an Error"),
            Poll::Pending => panic!("no ClientController request available"),
        };

        match request {
            wlan_policy::ClientControllerRequest::ScanForNetworks {
                iterator,
                control_handle: _,
            } => match iterator.into_stream() {
                Ok(stream) => stream,
                Err(e) => panic!("could not convert iterator into stream: {}", e),
            },
            _ => panic!("expecting a ScanForNetworks"),
        }
    }

    /// Sends scan results back to the client that is requesting a scan.
    fn send_scan_result(
        exec: &mut TestExecutor,
        server: &mut wlan_policy::ScanResultIteratorRequestStream,
        mut scan_result: &mut Result<Vec<wlan_policy::ScanResult>, wlan_policy::ScanErrorCode>,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut server.next()),
            Poll::Ready(Some(Ok(wlan_policy::ScanResultIteratorRequest::GetNext {
                responder
            }))) => {
                match responder.send(&mut scan_result) {
                    Ok(()) => {}
                    Err(e) => panic!("failed to send scan result: {}", e),
                }
            }
        );
    }

    /// Responds to a GetSavedNetworks request and provide an iterator for sending back saved
    /// networks.
    fn get_saved_networks_iterator(
        exec: &mut TestExecutor,
        mut server: wlan_policy::ClientControllerRequestStream,
    ) -> wlan_policy::NetworkConfigIteratorRequestStream {
        let poll = exec.run_until_stalled(&mut server.next());
        let request = match poll {
            Poll::Ready(poll_ready) => poll_ready
                .expect("poll ready result is None")
                .expect("poll ready result is an Error"),
            Poll::Pending => panic!("no ClientController request available"),
        };

        match request {
            wlan_policy::ClientControllerRequest::GetSavedNetworks {
                iterator,
                control_handle: _,
            } => match iterator.into_stream() {
                Ok(stream) => stream,
                Err(e) => panic!("could not convert iterator into stream: {}", e),
            },
            _ => panic!("expecting a ScanForNetworks"),
        }
    }

    /// Uses the provided iterator and send back saved networks.
    fn send_saved_networks(
        exec: &mut TestExecutor,
        server: &mut wlan_policy::NetworkConfigIteratorRequestStream,
        saved_networks_response: Vec<wlan_policy::NetworkConfig>,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut server.next()),
            Poll::Ready(Some(Ok(wlan_policy::NetworkConfigIteratorRequest::GetNext {
                responder
            }))) => {
                match responder.send(&mut saved_networks_response.into_iter()) {
                    Ok(()) => {}
                    Err(e) => panic!("failed to send saved networks: {}", e),
                }
            }
        );
    }

    /// Tests the case where start client connections is called and the operation is successful.
    #[fuchsia::test]
    fn test_start_client_connections_success() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = client_test_setup();
        let fut = handle_start_client_connections(test_values.client_proxy);
        pin_mut!(fut);

        // Wait for the fidl request to go out to start client connections
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back an acknowledgement
        send_client_request_status(
            &mut exec,
            &mut test_values.client_stream,
            fidl_wlan_common::RequestStatus::Acknowledged,
        );

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
    }

    /// Tests the case where starting client connections fails.
    #[fuchsia::test]
    fn test_start_client_connections_fail() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = client_test_setup();
        let fut = handle_start_client_connections(test_values.client_proxy);
        pin_mut!(fut);

        // Wait for the fidl request to go out to start client connections
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back an acknowledgement
        send_client_request_status(
            &mut exec,
            &mut test_values.client_stream,
            fidl_wlan_common::RequestStatus::RejectedNotSupported,
        );

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where starting client connections is successful.
    #[fuchsia::test]
    fn test_stop_client_connections_success() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = client_test_setup();
        let fut = handle_stop_client_connections(test_values.client_proxy);
        pin_mut!(fut);

        // Wait for the fidl request to go out to stop client connections
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back an acknowledgement
        send_client_request_status(
            &mut exec,
            &mut test_values.client_stream,
            fidl_wlan_common::RequestStatus::Acknowledged,
        );

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
    }

    /// Tests the case where starting client connections fails.
    #[fuchsia::test]
    fn test_stop_client_connections_fail() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = client_test_setup();
        let fut = handle_stop_client_connections(test_values.client_proxy);
        pin_mut!(fut);

        // Wait for the fidl request to go out to stop client connections
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back an acknowledgement
        send_client_request_status(
            &mut exec,
            &mut test_values.client_stream,
            fidl_wlan_common::RequestStatus::RejectedNotSupported,
        );

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where a network is successfully saved.
    #[fuchsia::test]
    fn test_save_network_pass() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = client_test_setup();
        let config = create_network_config(TEST_SSID);
        let fut = handle_save_network(test_values.client_proxy, config);
        pin_mut!(fut);

        // Wait for the fidl request to go out to save a network
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Drop the remote channel indicating success
        send_network_config_response(&mut exec, test_values.client_stream, true);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
    }

    /// Tests the case where a network configuration cannot be saved.
    #[fuchsia::test]
    fn test_save_network_fail() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = client_test_setup();
        let config = create_network_config(TEST_SSID);
        let fut = handle_save_network(test_values.client_proxy, config);
        pin_mut!(fut);

        // Wait for the fidl request to go out to save a network
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back an error
        send_network_config_response(&mut exec, test_values.client_stream, false);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where a network config can be successfully removed.
    #[fuchsia::test]
    fn test_remove_network_pass() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = client_test_setup();
        let config = create_network_config(TEST_SSID);
        let fut = handle_remove_network(test_values.client_proxy, config);
        pin_mut!(fut);

        // Wait for the fidl request to go out to remove a network
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Drop the remote channel indicating success
        send_network_config_response(&mut exec, test_values.client_stream, true);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
    }

    /// Tests the case where network removal fails.
    #[fuchsia::test]
    fn test_remove_network_fail() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = client_test_setup();
        let config = create_network_config(TEST_SSID);
        let fut = handle_remove_network(test_values.client_proxy, config);
        pin_mut!(fut);

        // Wait for the fidl request to go out to remove a network
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back an error
        send_network_config_response(&mut exec, test_values.client_stream, false);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where the client successfully connects.
    #[fuchsia::test]
    fn test_connect_pass() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = client_test_setup();

        // Start the connect routine.
        let config = create_network_id(TEST_SSID);
        let fut = handle_connect(test_values.client_proxy, test_values.update_stream, config);
        pin_mut!(fut);

        // The function should now stall out waiting on the connect call to go out
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back a positive acknowledgement
        send_client_request_status(
            &mut exec,
            &mut test_values.client_stream,
            fidl_wlan_common::RequestStatus::Acknowledged,
        );

        // The client should now wait for events from the listener.  Send a Connecting update.
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        let _ = test_values.update_proxy.on_client_state_update(create_client_state_summary(
            TEST_SSID,
            wlan_policy::ConnectionState::Connecting,
        ));

        // The client should stall and then wait for a Connected message.  Send that over.
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        let _ = test_values.update_proxy.on_client_state_update(create_client_state_summary(
            TEST_SSID,
            wlan_policy::ConnectionState::Connected,
        ));

        // The connect process should complete after receiving the Connected status response
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
    }

    /// Tests the case where a client fails to connect.
    #[fuchsia::test]
    fn test_connect_fail() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = client_test_setup();

        // Start the connect routine.
        let config = create_network_id(TEST_SSID);
        let fut = handle_connect(test_values.client_proxy, test_values.update_stream, config);
        pin_mut!(fut);

        // The function should now stall out waiting on the connect call to go out
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back a positive acknowledgement
        send_client_request_status(
            &mut exec,
            &mut test_values.client_stream,
            fidl_wlan_common::RequestStatus::Acknowledged,
        );

        // The client should now wait for events from the listener
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        let _ = test_values.update_proxy.on_client_state_update(create_client_state_summary(
            TEST_SSID,
            wlan_policy::ConnectionState::Failed,
        ));

        // The connect process should return an error after receiving a Failed status
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where a scan is requested and results are sent back to the requester.
    #[fuchsia::test]
    fn test_scan_pass() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = client_test_setup();

        let fut = handle_scan(test_values.client_proxy);
        pin_mut!(fut);

        // The function should now stall out waiting on the scan call to go out
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back a scan result
        let mut iterator = get_scan_result_iterator(&mut exec, test_values.client_stream);
        send_scan_result(&mut exec, &mut iterator, &mut Ok(vec![create_scan_result(TEST_SSID)]));

        // Process the scan result
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back an empty scan result
        send_scan_result(&mut exec, &mut iterator, &mut Ok(Vec::new()));

        // Expect the scan process to complete
        assert_variant!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(Ok(result)) => {
                assert_eq!(result.len(), 1);
                assert_eq!(result[0].id.as_ref().unwrap().ssid, TEST_SSID.as_bytes().to_vec());
            }
        );
    }

    /// Tests the case where the scan cannot be performed.
    #[fuchsia::test]
    fn test_scan_fail() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = client_test_setup();

        let fut = handle_scan(test_values.client_proxy);
        pin_mut!(fut);

        // The function should now stall out waiting on the scan call to go out
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back a scan error
        let mut iterator = get_scan_result_iterator(&mut exec, test_values.client_stream);
        send_scan_result(
            &mut exec,
            &mut iterator,
            &mut Err(wlan_policy::ScanErrorCode::GeneralError),
        );

        // Process the scan error
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    /// Tests the ability to get saved networks from the client policy layer.
    #[fuchsia::test]
    fn test_get_saved_networks() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = client_test_setup();

        let fut = handle_get_saved_networks(test_values.client_proxy);
        pin_mut!(fut);

        // The future should stall out waiting on the get saved networks request
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back a saved networks response
        let mut iterator = get_saved_networks_iterator(&mut exec, test_values.client_stream);
        send_saved_networks(&mut exec, &mut iterator, vec![create_network_config(TEST_SSID)]);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back an empty set of configs to indicate that the process is complete
        send_saved_networks(&mut exec, &mut iterator, vec![]);

        // Verify that the saved networks response was recorded properly
        assert_variant!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(Ok(result)) => {
                assert_eq!(result.len(), 1);
                assert_eq!(result[0].id.as_ref().unwrap().ssid, TEST_SSID.as_bytes().to_vec());
            }
        );
    }

    /// Tests to ensure that the listening loop continues to be active after receiving client state updates.
    #[fuchsia::test]
    fn test_client_listen() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = client_test_setup();

        let fut = handle_listen(test_values.update_stream);
        pin_mut!(fut);

        // Listen should stall waiting for updates
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        let _ = test_values.update_proxy.on_client_state_update(create_client_state_summary(
            TEST_SSID,
            wlan_policy::ConnectionState::Connecting,
        ));

        // Listen should process the message and stall again
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        let _ = test_values.update_proxy.on_client_state_update(create_client_state_summary(
            TEST_SSID,
            wlan_policy::ConnectionState::Connected,
        ));

        // Listener future should continue to run but stall waiting for more updates
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
    }

    /// Tests the case where an AP is requested to be stopped and the policy service returns an
    /// error.
    #[fuchsia::test]
    fn test_stop_ap_fail() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = ap_test_setup();

        let network_config = create_network_config(&TEST_SSID);
        let fut = handle_stop_ap(test_values.ap_proxy, network_config);
        pin_mut!(fut);

        // The request should stall waiting for the service
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back a rejection
        send_ap_request_status(
            &mut exec,
            &mut test_values.ap_stream,
            fidl_wlan_common::RequestStatus::RejectedNotSupported,
        );

        // Run the request to completion
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where an AP is successfully stopped.
    #[fuchsia::test]
    fn test_stop_ap_pass() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = ap_test_setup();

        let network_config = create_network_config(&TEST_SSID);
        let fut = handle_stop_ap(test_values.ap_proxy, network_config);
        pin_mut!(fut);

        // The request should stall waiting for the service
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back an acknowledgement
        send_ap_request_status(
            &mut exec,
            &mut test_values.ap_stream,
            fidl_wlan_common::RequestStatus::Acknowledged,
        );

        // Run the request to completion
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
    }

    /// Tests the case where the request to start an AP results in an error.
    #[fuchsia::test]
    fn test_start_ap_request_fail() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = ap_test_setup();

        let network_config = create_network_config(&TEST_SSID);
        let fut = handle_start_ap(test_values.ap_proxy, test_values.update_stream, network_config);
        pin_mut!(fut);

        // The request should stall waiting for the service
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back a rejection
        send_ap_request_status(
            &mut exec,
            &mut test_values.ap_stream,
            fidl_wlan_common::RequestStatus::RejectedNotSupported,
        );

        // Run the request to completion
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where the start AP process returns an acknowledgement.  The tool should then
    /// wait for an update indicating that the AP is active.
    #[fuchsia::test]
    fn test_start_ap_pass() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = ap_test_setup();

        let network_config = create_network_config(&TEST_SSID);
        let fut = handle_start_ap(test_values.ap_proxy, test_values.update_stream, network_config);
        pin_mut!(fut);

        // The request should stall waiting for the service
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back an acknowledgement
        send_ap_request_status(
            &mut exec,
            &mut test_values.ap_stream,
            fidl_wlan_common::RequestStatus::Acknowledged,
        );

        // Progress the future so that it waits for AP state updates
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // First send a `Starting` status.
        let mut state_updates = vec![
            create_ap_state_summary(wlan_policy::OperatingState::Starting),
            create_ap_state_summary(wlan_policy::OperatingState::Active),
        ];
        let _ =
            test_values.update_proxy.on_access_point_state_update(&mut state_updates.drain(..1));

        // Future should still be waiting to see that the AP to be active
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send the response indicating that the AP is active
        let _ = test_values.update_proxy.on_access_point_state_update(&mut state_updates.drain(..));

        // Run the request to completion
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
    }

    /// Tests the case where the AP start command is successfully sent, but the AP fails during
    /// the startup process.
    #[fuchsia::test]
    fn test_ap_failed_to_start() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = ap_test_setup();

        let network_config = create_network_config(&TEST_SSID);
        let fut = handle_start_ap(test_values.ap_proxy, test_values.update_stream, network_config);
        pin_mut!(fut);

        // The request should stall waiting for the service
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back an acknowledgement
        send_ap_request_status(
            &mut exec,
            &mut test_values.ap_stream,
            fidl_wlan_common::RequestStatus::Acknowledged,
        );

        // Progress the future so that it waits for AP state updates
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Send back a failure
        let mut state_updates = vec![create_ap_state_summary(wlan_policy::OperatingState::Failed)];
        let _ = test_values.update_proxy.on_access_point_state_update(&mut state_updates.drain(..));

        // Expect that the future returns an error
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where all APs are requested to be stopped.
    #[fuchsia::test]
    fn test_stop_all_aps() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = ap_test_setup();

        let fut = handle_stop_all_aps(test_values.ap_proxy);
        pin_mut!(fut);

        // The future should finish immediately
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));

        // Make sure that the request is seen on the request stream
        assert_variant!(
            exec.run_until_stalled(&mut test_values.ap_stream.next()),
            Poll::Ready(Some(Ok(
                wlan_policy::AccessPointControllerRequest::StopAllAccessPoints { .. }
            )))
        );
    }

    /// Tests that the AP listen routine continues listening for new updates.
    #[fuchsia::test]
    fn test_ap_listen() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = ap_test_setup();

        let mut state_updates = vec![
            create_ap_state_summary(wlan_policy::OperatingState::Starting),
            create_ap_state_summary(wlan_policy::OperatingState::Active),
            create_ap_state_summary(wlan_policy::OperatingState::Failed),
        ];

        let fut = handle_ap_listen(test_values.update_stream);
        pin_mut!(fut);

        // Listen should stall waiting for updates
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        let _ =
            test_values.update_proxy.on_access_point_state_update(&mut state_updates.drain(..1));

        // Listen should process the message and stall again
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        let _ =
            test_values.update_proxy.on_access_point_state_update(&mut state_updates.drain(..1));

        // Process message and stall again
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        let _ =
            test_values.update_proxy.on_access_point_state_update(&mut state_updates.drain(..1));

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
    }

    #[fuchsia::test]
    fn test_suggest_ap_mac_succeeds() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");

        let (configurator_proxy, mut configurator_stream) =
            endpoints::create_proxy_and_stream::<wlan_deprecated::DeprecatedConfiguratorMarker>()
                .expect("failed to create DeprecatedConfigurator proxy");
        let mac = MacAddress::from_bytes(&[0, 1, 2, 3, 4, 5]).unwrap();
        let suggest_fut = handle_suggest_ap_mac(configurator_proxy, mac);
        pin_mut!(suggest_fut);

        assert_variant!(exec.run_until_stalled(&mut suggest_fut), Poll::Pending);

        assert_variant!(
            exec.run_until_stalled(&mut configurator_stream.next()),
            Poll::Ready(Some(Ok(wlan_deprecated::DeprecatedConfiguratorRequest::SuggestAccessPointMacAddress {
                mac: fidl_fuchsia_net::MacAddress { octets: [0, 1, 2, 3, 4, 5] }, responder
            }))) => {
                assert!(responder.send(&mut Ok(())).is_ok());
            }
        );

        assert_variant!(exec.run_until_stalled(&mut suggest_fut), Poll::Ready(Ok(())));
    }

    #[fuchsia::test]
    fn test_suggest_ap_mac_fails() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");

        let (configurator_proxy, mut configurator_stream) =
            endpoints::create_proxy_and_stream::<wlan_deprecated::DeprecatedConfiguratorMarker>()
                .expect("failed to create DeprecatedConfigurator proxy");
        let mac = MacAddress::from_bytes(&[0, 1, 2, 3, 4, 5]).unwrap();
        let suggest_fut = handle_suggest_ap_mac(configurator_proxy, mac);
        pin_mut!(suggest_fut);

        assert_variant!(exec.run_until_stalled(&mut suggest_fut), Poll::Pending);

        assert_variant!(
            exec.run_until_stalled(&mut configurator_stream.next()),
            Poll::Ready(Some(Ok(wlan_deprecated::DeprecatedConfiguratorRequest::SuggestAccessPointMacAddress {
                mac: fidl_fuchsia_net::MacAddress { octets: [0, 1, 2, 3, 4, 5] }, responder
            }))) => {
                assert!(responder.send(&mut Err(wlan_deprecated::SuggestMacAddressError::InvalidArguments)).is_ok());
            }
        );

        assert_variant!(exec.run_until_stalled(&mut suggest_fut), Poll::Ready(Err(_)));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_proxy_command_succeeds() {
        match run_proxy_command(Box::pin(async { Ok(zx_status::Status::OK) })).await {
            Ok(status) => {
                assert_eq!(status, zx_status::Status::OK)
            }
            Err(e) => panic!("Test unexpectedly failed with {}", e),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_proxy_command_already_bound() {
        let result: Result<(), Error> = run_proxy_command(Box::pin(async {
            Err(fidl::Error::ClientChannelClosed {
                status: zx_status::Status::ALREADY_BOUND,
                protocol_name: "test",
            })
        }))
        .await;
        match result {
            Ok(status) => panic!("Test unexpectedly succeeded with {:?}", status),
            Err(e) => {
                assert!(e.to_string().contains("Failed to obtain a WLAN policy controller"));
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_proxy_command_generic_failure() {
        let result: Result<(), Error> =
            run_proxy_command(Box::pin(async { Err(fidl::Error::Invalid) })).await;
        match result {
            Ok(status) => panic!("Test unexpectedly succeeded with {:?}", status),
            Err(e) => {
                assert!(!e.to_string().contains("Failed to obtain a WLAN policy controller"));
            }
        }
    }
}
