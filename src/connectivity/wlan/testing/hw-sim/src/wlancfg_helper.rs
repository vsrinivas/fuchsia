// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::test_utils::RetryWithBackoff,
    fidl::endpoints::{create_endpoints, create_proxy},
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy as fidl_policy,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::prelude::*,
    futures::StreamExt,
};

// Holds basic WLAN network configuration information and allows cloning and conversion to a policy
// NetworkConfig.
#[derive(Clone)]
pub struct NetworkConfigBuilder {
    ssid: Option<Vec<u8>>,
    password: Option<Vec<u8>>,
}

impl NetworkConfigBuilder {
    pub fn open() -> Self {
        Self { password: None, ssid: None }
    }

    pub fn protected(password: &Vec<u8>) -> Self {
        Self { password: Some(password.to_vec()), ssid: None }
    }

    pub fn ssid(self, ssid: &Vec<u8>) -> Self {
        Self { ssid: Some(ssid.to_vec()), ..self }
    }
}

impl From<NetworkConfigBuilder> for fidl_policy::NetworkConfig {
    fn from(config: NetworkConfigBuilder) -> fidl_policy::NetworkConfig {
        let ssid = match config.ssid {
            None => vec![],
            Some(ssid) => ssid,
        };

        let (type_, credential) = match config.password {
            None => {
                (fidl_policy::SecurityType::None, fidl_policy::Credential::None(fidl_policy::Empty))
            }
            Some(password) => {
                (fidl_policy::SecurityType::Wpa2, fidl_policy::Credential::Password(password))
            }
        };

        fidl_policy::NetworkConfig {
            id: Some(fidl_policy::NetworkIdentifier { ssid, type_ }),
            credential: Some(credential),
        }
    }
}

pub async fn start_ap_and_wait_for_confirmation(network_config: NetworkConfigBuilder) {
    let network_config = NetworkConfigBuilder::from(network_config);

    // Get a handle to control the AccessPointController.
    let ap_provider = connect_to_service::<fidl_policy::AccessPointProviderMarker>()
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

        let slept = retry.sleep_unless_timed_out().await;
        assert!(slept, "unable to create AP iface");
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
pub fn init_client_controller(
) -> (fidl_policy::ClientControllerProxy, fidl_policy::ClientStateUpdatesRequestStream) {
    let provider = connect_to_service::<fidl_policy::ClientProviderMarker>().unwrap();
    let (controller_client_end, controller_server_end) = fidl::endpoints::create_proxy().unwrap();
    let (listener_client_end, listener_server_end) = fidl::endpoints::create_endpoints().unwrap();
    provider.get_controller(controller_server_end, listener_client_end).unwrap();
    let listener_stream = listener_server_end.into_stream().unwrap();
    (controller_client_end, listener_stream)
}

/// Creates a listener update stream for getting status updates.
pub fn init_client_listener() -> fidl_policy::ClientStateUpdatesRequestStream {
    let listener = connect_to_service::<fidl_policy::ClientListenerMarker>().unwrap();
    let (client_end, server_end) = fidl::endpoints::create_endpoints().unwrap();
    listener.get_listener(client_end).unwrap();
    let listener_stream = server_end.into_stream().unwrap();
    listener_stream
}

/// Get the next client update. Will panic if no updates are available.
pub async fn get_update_from_client_listener(
    update_listener: &mut fidl_policy::ClientStateUpdatesRequestStream,
) -> fidl_policy::ClientStateSummary {
    let update_request = update_listener
        .next()
        .await
        .expect("ClientStateUpdatesRequestStream failed")
        .expect("ClientStateUpdatesRequestStream received invalid update");
    let update = update_request.into_on_client_state_update();
    let (update, responder) = update.expect("Client provider produced invalid update.");
    // Ack the update.
    responder.send().expect("failed to ack update");
    update.into()
}
