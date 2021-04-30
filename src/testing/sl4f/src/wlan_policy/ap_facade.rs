// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::wlan_policy::types::AccessPointState,
    anyhow::Error,
    fidl::endpoints::{create_endpoints, create_proxy},
    fidl_fuchsia_wlan_common::RequestStatus,
    fidl_fuchsia_wlan_policy::{
        AccessPointControllerMarker, AccessPointControllerProxy, AccessPointListenerMarker,
        AccessPointProviderMarker, AccessPointStateUpdatesMarker,
        AccessPointStateUpdatesRequestStream, ConnectivityMode, Credential, NetworkConfig,
        NetworkIdentifier, OperatingBand, OperatingState, SecurityType,
    },
    fuchsia_component::client::connect_to_protocol,
    futures::TryStreamExt,
    std::{
        cell::Cell,
        fmt::{self, Debug},
    },
};

pub struct WlanApPolicyFacade {
    ap_controller: AccessPointControllerProxy,
    update_listener: Cell<Option<AccessPointStateUpdatesRequestStream>>,
}

impl Debug for WlanApPolicyFacade {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let listener = self.update_listener.take();
        let update_listener =
            if listener.is_some() { "Some(AccessPointStateUpdatesRequestStream)" } else { "None" }
                .to_string();
        self.update_listener.set(listener);

        f.debug_struct("WlanApPolicyFacade")
            .field("controller", &self.ap_controller)
            .field("update_listener", &update_listener)
            .finish()
    }
}

impl WlanApPolicyFacade {
    pub fn new() -> Result<WlanApPolicyFacade, Error> {
        let policy_provider = connect_to_protocol::<AccessPointProviderMarker>()?;
        let (ap_controller, server_end) = create_proxy::<AccessPointControllerMarker>().unwrap();

        let (update_client_end, update_listener) =
            create_endpoints::<AccessPointStateUpdatesMarker>().unwrap();
        let () = policy_provider.get_controller(server_end, update_client_end)?;
        let update_stream = update_listener.into_stream()?;
        Ok(WlanApPolicyFacade { ap_controller, update_listener: Cell::new(Some(update_stream)) })
    }

    pub async fn start_access_point(
        &self,
        target_ssid: Vec<u8>,
        type_: SecurityType,
        credential: Credential,
        mode: ConnectivityMode,
        band: OperatingBand,
    ) -> Result<(), Error> {
        let listener = connect_to_protocol::<AccessPointListenerMarker>()?;
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
                NetworkConfig {
                    id: Some(network_id),
                    credential: Some(credential),
                    ..NetworkConfig::EMPTY
                },
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
            .stop_access_point(NetworkConfig {
                id: Some(network_id),
                credential: Some(credential),
                ..NetworkConfig::EMPTY
            })
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

    /// Creates a listener update stream for getting status updates.
    fn init_listener() -> Result<AccessPointStateUpdatesRequestStream, Error> {
        let listener = connect_to_protocol::<AccessPointListenerMarker>()?;
        let (client_end, server_end) =
            fidl::endpoints::create_endpoints::<AccessPointStateUpdatesMarker>().unwrap();
        listener.get_listener(client_end)?;
        Ok(server_end.into_stream()?)
    }

    /// This function will set a new listener even if there is one because new listeners will get
    /// the most recent update immediately without waiting.
    pub fn set_new_listener(&self) -> Result<(), Error> {
        self.update_listener.set(Some(Self::init_listener()?));
        Ok(())
    }

    /// Wait for and return an AP update. If this is the first update gotten from the facade
    /// since the AP controller or a new update listener has been created, it will get an
    /// immediate status. After that, it will wait for a change and return a status when there has
    /// been a change since the last call to get_update. This call will hang if there are no
    /// updates.
    /// This function is not thread safe, so there should not be multiple get_update calls at the
    /// same time unless a new listener is set between them. There is no lock around the
    /// update_listener field of the facade in order to prevent a hanging get_update from blocking
    /// all future get_updates.
    pub async fn get_update(&self) -> Result<Vec<AccessPointState>, Error> {
        // Initialize the update listener if it has not been initialized.
        let listener = self.update_listener.take();
        let mut update_listener = if listener.is_none() {
            Self::init_listener()
        } else {
            listener.ok_or(format_err!("failed to set update listener of facade"))
        }?;

        if let Some(update_request) = update_listener.try_next().await? {
            let update = update_request.into_on_access_point_state_update();
            let (update, responder) = match update {
                Some((update, responder)) => (update, responder),
                None => return Err(format_err!("Client provider produced invalid update.")).into(),
            };
            // Ack the update.
            responder.send().map_err(|e| format_err!("failed to ack update: {}", e))?;
            // Put the update listener back in the facade
            self.update_listener.set(Some(update_listener));

            let update = update.into_iter().map(|update| AccessPointState::from(update)).collect();
            Ok(update)
        } else {
            self.update_listener.set(Some(update_listener));
            Err(format_err!("update listener's next update is None"))
        }
    }
}
