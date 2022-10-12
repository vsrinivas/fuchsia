// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::test_utils::RetryWithBackoff,
    fidl::endpoints::{create_endpoints, create_proxy},
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_policy::{Credential, NetworkConfig, NetworkIdentifier, SecurityType},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon::prelude::*,
    futures::{StreamExt, TryStreamExt},
    ieee80211::Ssid,
    std::convert::TryInto,
    tracing::info,
};

fn create_network_config(
    ssid: &Ssid,
    security_type: SecurityType,
    credential: Credential,
) -> NetworkConfig {
    let network_id = NetworkIdentifier { ssid: ssid.to_vec(), type_: security_type };
    NetworkConfig { id: Some(network_id), credential: Some(credential), ..NetworkConfig::EMPTY }
}

fn credential_to_string(credential: &Credential) -> String {
    match credential {
        Credential::None(_) => String::from("None"),
        Credential::Password(password) => {
            format!("{} (type: password)", String::from_utf8_lossy(password))
        }
        Credential::Psk(psk) => format!("{} (type: psk)", String::from_utf8_lossy(psk)),
        &_ => unimplemented!(),
    }
}

// Holds basic WLAN network configuration information and allows cloning and conversion to a policy
// NetworkConfig.
#[derive(Clone)]
pub struct NetworkConfigBuilder {
    ssid: Option<Ssid>,
    security_type: fidl_policy::SecurityType,
    password: Option<Vec<u8>>,
}

impl NetworkConfigBuilder {
    pub fn open() -> Self {
        Self { ssid: None, security_type: fidl_policy::SecurityType::None, password: None }
    }

    pub fn protected(security_type: fidl_policy::SecurityType, password: &Vec<u8>) -> Self {
        assert!(
            fidl_policy::SecurityType::None != security_type,
            "security_type for NetworkConfigBuilder::protected() cannot be None"
        );
        Self { ssid: None, security_type, password: Some(password.to_vec()) }
    }

    pub fn ssid(self, ssid: &Ssid) -> Self {
        Self { ssid: Some(ssid.clone()), ..self }
    }
}

impl From<NetworkConfigBuilder> for fidl_policy::NetworkConfig {
    fn from(config: NetworkConfigBuilder) -> fidl_policy::NetworkConfig {
        let ssid = match config.ssid {
            None => Ssid::empty(),
            Some(ssid) => ssid,
        };

        let (type_, credential) = match (config.security_type, config.password) {
            (fidl_policy::SecurityType::None, Some(_)) => {
                panic!("Password provided with fidl_policy::SecurityType::None")
            }
            (fidl_policy::SecurityType::None, None) => {
                (fidl_policy::SecurityType::None, fidl_policy::Credential::None(fidl_policy::Empty))
            }
            (security_type, Some(password)) => {
                (security_type, fidl_policy::Credential::Password(password))
            }
            (_, None) => {
                panic!("Password not provided with fidl_policy::SecurityType other than None")
            }
        };

        fidl_policy::NetworkConfig {
            id: Some(fidl_policy::NetworkIdentifier { ssid: ssid.into(), type_ }),
            credential: Some(credential),
            ..fidl_policy::NetworkConfig::EMPTY
        }
    }
}

pub async fn start_ap_and_wait_for_confirmation(network_config: NetworkConfigBuilder) {
    let network_config = NetworkConfigBuilder::from(network_config);

    // Get a handle to control the AccessPointController.
    let ap_provider = connect_to_protocol::<fidl_policy::AccessPointProviderMarker>()
        .expect("connecting to AP provider");
    let (ap_controller, server_end) =
        create_proxy::<fidl_policy::AccessPointControllerMarker>().expect("creating AP controller");
    let (update_client_end, update_server_end) =
        create_endpoints::<fidl_policy::AccessPointStateUpdatesMarker>()
            .expect("creating AP update listener");
    let () =
        ap_provider.get_controller(server_end, update_client_end).expect("getting AP controller");

    // Clear the initial update to ensure that the 'Active' update is received once the AP is
    // started.
    let mut update_stream =
        update_server_end.into_stream().expect("could not create update stream");
    let initial_update = update_stream.next().await.expect("AP update stream failed");

    // The initial update is empty since no AP ifaces have been created yet.  All that is required
    // is to ACK the initial update.
    let (_updates, responder) = initial_update
        .expect("received invalid update")
        .into_on_access_point_state_update()
        .expect("AP provider produced invalid update.");
    let () = responder.send().expect("failed to send update response");

    // Start the AP
    let mut retry = RetryWithBackoff::new(120.seconds());
    loop {
        let controller = ap_controller.clone();

        // Call StartAccessPoint.  If the policy layer does not yet have an ApSmeProxy, it
        // it will attempt to create an AP interface.
        let config = fidl_policy::NetworkConfig::from(network_config.clone());
        let connectivity_mode = fidl_policy::ConnectivityMode::Unrestricted;
        let operating_band = fidl_policy::OperatingBand::Any;

        // If the policy layer acknowledges the request to start the access point, then the
        // AP interface has been created.
        match controller
            .start_access_point(config, connectivity_mode, operating_band)
            .await
            .expect("starting AP")
        {
            fidl_common::RequestStatus::Acknowledged => break,
            _ => (),
        }

        retry.sleep_unless_after_deadline().await.expect("unable to create AP iface");
    }

    // Wait until the policy service notifies that the AP has started.
    while let Ok(update_request) = update_stream.next().await.expect("AP update stream failed") {
        let (updates, responder) = update_request
            .into_on_access_point_state_update()
            .expect("AP provider produced invalid update.");

        let () = responder.send().expect("failed to send update response");

        for update in updates {
            match update.state {
                Some(fidl_policy::OperatingState::Failed) => panic!("Failed to start AP."),
                Some(fidl_policy::OperatingState::Starting) | None => (),
                Some(fidl_policy::OperatingState::Active) => return,
            }
        }
    }
    panic!("update stream ended unexpectedly");
}

/// Creates a client controller and update stream for getting status updates.
pub async fn init_client_controller(
) -> (fidl_policy::ClientControllerProxy, fidl_policy::ClientStateUpdatesRequestStream) {
    let provider = connect_to_protocol::<fidl_policy::ClientProviderMarker>().unwrap();
    let (controller_client_end, controller_server_end) = fidl::endpoints::create_proxy().unwrap();
    let (listener_client_end, listener_server_end) = fidl::endpoints::create_endpoints().unwrap();
    provider.get_controller(controller_server_end, listener_client_end).unwrap();
    let mut client_state_update_stream = listener_server_end.into_stream().unwrap();

    // Clear the initial state notifications that are sent by the policy layer.  This initial state
    // is variable depending on whether or not the policy layer has discovered a PHY or created an
    // interface yet.  The policy layer enables client connections by default.  Wait until the
    // first update that indicates that client connections are enabled before proceeding.
    wait_until_client_state(&mut client_state_update_stream, |update| {
        if update.state == Some(fidl_policy::WlanClientState::ConnectionsEnabled) {
            return true;
        } else {
            info!("Awaiting client state ConnectionsEnabled, got {:?}", update.state);
            return false;
        }
    })
    .await;

    (controller_client_end, client_state_update_stream)
}

// Wait until the policy layer returns an update for which `continue_fn` returns true.
// This function will panic if the `client_state_update_stream` stream ends.
pub async fn wait_until_client_state<F: Fn(fidl_policy::ClientStateSummary) -> bool>(
    client_state_update_stream: &mut fidl_policy::ClientStateUpdatesRequestStream,
    continue_fn: F,
) {
    while let Some(update_request) =
        client_state_update_stream.try_next().await.expect("getting state update")
    {
        let (update, responder) =
            update_request.into_on_client_state_update().expect("converting to state update");
        let _ = responder.send();
        if continue_fn(update) {
            return;
        };
    }
    panic!("The stream unexpectedly terminated");
}

pub fn has_id_and_state(
    update: fidl_policy::ClientStateSummary,
    id_to_match: &NetworkIdentifier,
    state_to_match: fidl_policy::ConnectionState,
) -> bool {
    for net_state in update.networks.expect("getting client networks") {
        let id = net_state.id.expect("empty network ID");
        let state = net_state.state.expect("empty network state");

        if &id == id_to_match && state == state_to_match {
            return true;
        }
    }
    return false;
}

pub async fn save_network(
    client_controller: &fidl_policy::ClientControllerProxy,
    ssid: &Ssid,
    security_type: SecurityType,
    credential: fidl_policy::Credential,
) {
    info!("Saving network. SSID: {:?}, Credential: {:?}", ssid, credential_to_string(&credential),);
    let network_config = create_network_config(ssid, security_type, credential);
    client_controller
        .save_network(network_config)
        .await
        .expect("save_network future failed")
        .expect("client controller failed to save network");
}

pub async fn remove_network(
    client_controller: &fidl_policy::ClientControllerProxy,
    ssid: &Ssid,
    security_type: SecurityType,
    credential: fidl_policy::Credential,
) {
    info!(
        "Removing network. SSID: {}, Credential: {:?}",
        ssid.to_string_not_redactable(),
        credential_to_string(&credential)
    );
    let network_config = create_network_config(ssid, security_type, credential);
    client_controller
        .remove_network(network_config)
        .await
        .expect("remove_network future failed")
        .expect("client controller failed to remove network");
}

pub async fn remove_all_networks(client_controller: &fidl_policy::ClientControllerProxy) {
    info!("Removing all saved networks");
    let mut saved_networks = vec![];

    // Read all saved networks on the device
    let (iter, server) =
        fidl::endpoints::create_proxy::<fidl_policy::NetworkConfigIteratorMarker>()
            .expect("failed to create iterator");
    client_controller.get_saved_networks(server).expect("Failed to call get saved networks");
    loop {
        let additional_saved_networks =
            iter.get_next().await.expect("Failed to read saved networks");
        if additional_saved_networks.len() == 0 {
            break;
        } else {
            saved_networks.extend(additional_saved_networks);
        }
    }

    // Remove each saved network
    for network in saved_networks {
        remove_network(
            client_controller,
            &network.id.clone().unwrap().ssid.try_into().unwrap(),
            network.id.unwrap().type_.into(),
            network.credential.unwrap(),
        )
        .await;
    }
}

pub async fn await_failed(
    mut client_state_update_stream: &mut fidl_policy::ClientStateUpdatesRequestStream,
    network_identifier: fidl_policy::NetworkIdentifier,
    disconnect_status: fidl_policy::DisconnectStatus,
) {
    wait_until_client_state(&mut client_state_update_stream, |update| {
        if has_id_and_state(
            update.clone(),
            &network_identifier,
            fidl_policy::ConnectionState::Failed,
        ) {
            assert_eq!(
                update.networks,
                Some(vec![fidl_policy::NetworkState {
                    id: Some(network_identifier.clone()),
                    state: Some(fidl_policy::ConnectionState::Failed),
                    status: Some(disconnect_status),
                    ..fidl_policy::NetworkState::EMPTY
                }])
            );
            true
        } else {
            false
        }
    })
    .await;
}
