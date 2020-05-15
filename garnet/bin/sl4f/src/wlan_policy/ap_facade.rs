// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{create_endpoints, create_proxy},
    fidl_fuchsia_wlan_common::RequestStatus,
    fidl_fuchsia_wlan_policy::{
        AccessPointControllerMarker, AccessPointControllerProxy, AccessPointListenerMarker,
        AccessPointProviderMarker, AccessPointProviderProxy, AccessPointStateUpdatesMarker,
        ConnectivityMode, Credential, NetworkConfig, NetworkIdentifier, OperatingBand,
        OperatingState, SecurityType,
    },
    fuchsia_component::client::connect_to_service,
    futures::TryStreamExt,
};

#[derive(Debug)]
pub struct WlanApPolicyFacade {
    policy_provider: AccessPointProviderProxy,
    ap_controller: AccessPointControllerProxy,
}

impl WlanApPolicyFacade {
    pub fn new() -> Result<WlanApPolicyFacade, Error> {
        let policy_provider = connect_to_service::<AccessPointProviderMarker>()?;
        let (ap_controller, server_end) = create_proxy::<AccessPointControllerMarker>().unwrap();

        // Drop the initial client state update server end.  New update request streams will be
        // created for calls that rely on state changes.
        let (update_client_end, _) = create_endpoints::<AccessPointStateUpdatesMarker>().unwrap();
        let () = policy_provider.get_controller(server_end, update_client_end)?;
        Ok(WlanApPolicyFacade { policy_provider, ap_controller })
    }

    pub async fn start_access_point(
        &self,
        target_ssid: Vec<u8>,
        type_: SecurityType,
        credential: Credential,
        mode: ConnectivityMode,
        band: OperatingBand,
    ) -> Result<(), Error> {
        let listener = connect_to_service::<AccessPointListenerMarker>()?;
        let (client_end, server_end) = create_endpoints::<AccessPointStateUpdatesMarker>().unwrap();
        listener.get_listener(client_end)?;
        let mut server_stream = server_end.into_stream()?;

        match server_stream.try_next().await? {
            Some(update) => {
                let update = update.into_on_access_point_state_update();
                let (_, responder) = match update {
                    Some((update, responder)) => (update, responder),
                    None => return Err(format_err!("AP provider produced invalid update.")),
                };
                let _ = responder.send();
            }
            None => return Err(format_err!("initial steam already busted")),
        }

        let network_id = NetworkIdentifier { ssid: target_ssid.clone(), type_: type_ };

        match self
            .ap_controller
            .start_access_point(
                NetworkConfig { id: Some(network_id), credential: Some(credential) },
                mode,
                band,
            )
            .await?
        {
            RequestStatus::Acknowledged => {}
            RequestStatus::RejectedNotSupported => {
                return Err(format_err!("failed to start AP (not supported)"))
            }
            RequestStatus::RejectedIncompatibleMode => {
                return Err(format_err!("failed to start AP (incompatible mode)"))
            }
            RequestStatus::RejectedAlreadyInUse => {
                return Err(format_err!("failed to start AP (already in use)"))
            }
            RequestStatus::RejectedDuplicateRequest => {
                return Err(format_err!("failed to start AP (duplicate request)"))
            }
        }

        while let Some(update_request) = server_stream.try_next().await.unwrap() {
            let update = update_request.into_on_access_point_state_update();
            let (updates, responder) = match update {
                Some((update, responder)) => (update, responder),
                None => return Err(format_err!("AP provider produced invalid update.")),
            };
            let _ = responder.send();

            for update in updates {
                match update.state {
                    Some(state) => match state {
                        OperatingState::Failed => {
                            return Err(format_err!("Failed to start AP."));
                        }
                        OperatingState::Starting => {
                            continue;
                        }
                        OperatingState::Active => return Ok(()),
                    },
                    None => continue,
                }
            }
        }
        return Err(format_err!("AP update stream failed unexpectedly"));
    }

    pub async fn stop_access_point(
        &self,
        target_ssid: Vec<u8>,
        type_: SecurityType,
        credential: Credential,
    ) -> Result<(), Error> {
        let network_id = NetworkIdentifier { ssid: target_ssid.clone(), type_: type_ };
        match self
            .ap_controller
            .stop_access_point(NetworkConfig { id: Some(network_id), credential: Some(credential) })
            .await?
        {
            RequestStatus::Acknowledged => Ok(()),
            RequestStatus::RejectedNotSupported => {
                Err(format_err!("Failed to stop AP (not supported)"))
            }
            RequestStatus::RejectedIncompatibleMode => {
                Err(format_err!("Failed to stop AP (incompatible mode)"))
            }
            RequestStatus::RejectedAlreadyInUse => {
                Err(format_err!("Failed to stop AP (already in use)"))
            }
            RequestStatus::RejectedDuplicateRequest => {
                Err(format_err!("Failed to stop AP (duplicate request)"))
            }
        }
    }

    pub async fn stop_all_access_points(&self) -> Result<(), Error> {
        self.ap_controller.stop_all_access_points()?;
        Ok(())
    }
}
