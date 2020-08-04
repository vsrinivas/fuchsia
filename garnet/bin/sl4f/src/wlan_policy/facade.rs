// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        common_utils::common::macros::with_line,
        wlan_policy::types::{ClientStateSummary, NetworkConfig},
    },
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy as fidl_policy,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::macros::*,
    futures::TryStreamExt,
    parking_lot::RwLock,
    std::{
        cell::Cell,
        collections::HashSet,
        fmt::{self, Debug},
    },
};

pub struct WlanPolicyFacade {
    controller: RwLock<InnerController>,
    update_listener: Cell<Option<fidl_policy::ClientStateUpdatesRequestStream>>,
}

#[derive(Debug)]
pub struct InnerController {
    inner: Option<fidl_policy::ClientControllerProxy>,
}

impl Debug for WlanPolicyFacade {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let listener = self.update_listener.take();
        let update_listener =
            if listener.is_some() { "Some(ClientStateUpdatesRequestStream)" } else { "None" }
                .to_string();
        self.update_listener.set(listener);

        f.debug_struct("InnerWlanPolicyFacade")
            .field("controller", &self.controller)
            .field("update_listener", &update_listener)
            .finish()
    }
}

impl WlanPolicyFacade {
    pub fn new() -> Result<WlanPolicyFacade, Error> {
        Ok(Self {
            controller: RwLock::new(InnerController { inner: None }),
            update_listener: Cell::new(None),
        })
    }

    /// Client controller needs to be created once per server before the client controller API
    /// can be used. This will get the client conrtoller if has not already been initialized, if it
    /// has been initialize it does nothing.
    pub fn create_client_controller(&self) -> Result<(), Error> {
        let tag = "WlanPolicyFacade::create_client_controller";
        let mut controller_guard = self.controller.write();
        if let Some(controller) = controller_guard.inner.as_ref() {
            fx_log_info!(tag: &with_line!(tag), "Current client controller: {:?}", controller);
        } else {
            // Get controller
            fx_log_info!(tag: &with_line!(tag), "Setting new client controller");
            let (controller, update_stream) = Self::init_client_controller().map_err(|e| {
                fx_log_info!(tag: &with_line!(tag), "Error getting client controller: {}", e);
                format_err!("Error getting client,controller: {}", e)
            })?;
            controller_guard.inner = Some(controller);
            // Do not set value if it has already been set by getting updates.
            let update_listener = self.update_listener.take();
            if update_listener.is_none() {
                self.update_listener.set(Some(update_stream));
            } else {
                self.update_listener.set(update_listener);
            }
        }
        Ok(())
    }

    /// Creates and returns a client controller. This also returns the stream for listener updates
    /// that is created in the process of creating the client controller.
    fn init_client_controller() -> Result<
        (fidl_policy::ClientControllerProxy, fidl_policy::ClientStateUpdatesRequestStream),
        Error,
    > {
        let provider = connect_to_service::<fidl_policy::ClientProviderMarker>()?;
        let (controller, req) =
            fidl::endpoints::create_proxy::<fidl_policy::ClientControllerMarker>()?;
        let (update_sink, update_stream) =
            fidl::endpoints::create_request_stream::<fidl_policy::ClientStateUpdatesMarker>()?;
        provider.get_controller(req, update_sink)?;
        Ok((controller, update_stream))
    }

    /// Creates a listener update stream for getting status updates.
    fn init_listener() -> Result<fidl_policy::ClientStateUpdatesRequestStream, Error> {
        let listener = connect_to_service::<fidl_policy::ClientListenerMarker>()?;
        let (client_end, server_end) =
            fidl::endpoints::create_endpoints::<fidl_policy::ClientStateUpdatesMarker>().unwrap();
        listener.get_listener(client_end)?;
        Ok(server_end.into_stream()?)
    }

    /// This function will set a new listener even if there is one because new listeners will get
    /// the most recent update immediately without waiting. This might be used to set the facade's
    /// listener update stream if it wasn't set by creating the client controller or to set a clean
    /// state for a new test.
    pub fn set_new_listener(&self) -> Result<(), Error> {
        self.update_listener.set(Some(Self::init_listener()?));
        Ok(())
    }

    /// Request a scan and return the list of network names found, or an error if one occurs.
    pub async fn scan_for_networks(&self) -> Result<Vec<String>, Error> {
        let controller_guard = self.controller.read();
        let controller = controller_guard
            .inner
            .as_ref()
            .ok_or(format_err!("client controller has not been initialized"))?;

        // Policy will send results back through this iterator
        let (iter, server) =
            fidl::endpoints::create_proxy::<fidl_policy::ScanResultIteratorMarker>()
                .context("failed to create iterator")?;
        // Request a scan from policy
        controller.scan_for_networks(server)?;

        // Get results and check for scan error. Get the next chunk of results until we get an
        // error or empty list, which indicates the end of results.
        let mut scan_results = HashSet::new();
        loop {
            let results = iter.get_next().await?.map_err(|e| format_err!("{:?}", e))?;
            if results.is_empty() {
                break;
            }

            // For now, just return the names of the scanned networks.
            let results = Self::stringify_scan_results(results);
            scan_results.extend(results);
        }
        Ok(scan_results.into_iter().collect())
    }

    /// Connect to a network through the policy layer. The network muct have been saved first.
    /// Returns an error if the connect command was not recieved, otherwise it returns a boolean
    /// representing whether the connection was successful. Until listener updates are used, it
    /// will return false.
    /// NOTE - this will trigger a connection but will not yet return whether connection happened.
    /// # Arguments:
    /// * `target_ssid': The SSID (network name) that we want to connect to.
    /// * `type`: Security type should be a string of the security type, either "none", "wep",
    ///           "wpa", "wpa2" or "wpa3", matching the policy API's defined security types, case
    ///           doesn't matter.
    pub async fn connect(
        &self,
        target_ssid: Vec<u8>,
        type_: fidl_policy::SecurityType,
    ) -> Result<bool, Error> {
        let controller_guard = self.controller.read();
        let controller = controller_guard
            .inner
            .as_ref()
            .ok_or(format_err!("client controller has not been initialized"))?;

        let mut network_id = fidl_policy::NetworkIdentifier { ssid: target_ssid, type_ };
        match controller.connect(&mut network_id).await.expect("Connect: failed to connect") {
            fidl_common::RequestStatus::Acknowledged => {
                // For now we will just check whether our request is acknowledged.
                return Ok(false);
            }
            fidl_common::RequestStatus::RejectedNotSupported => bail!("RejectedNotSupported"),
            _ => {
                bail!("Scan: failed to send request");
            }
        }
    }

    /// Forget the specified saved network. Doesn't do anything if network not saved.
    /// # Arguments:
    /// * `target_ssid`:  The SSID (network name) that we want to forget.
    /// * `type`: the security type of the network. It should be a string, either "none", "wep",
    ///           "wpa", "wpa2" or "wpa3", matching the policy API's defined security types. Target
    ///           password can be password, PSK, or none, represented by empty string.
    /// * `credential`: the password or other credential of the network we want to forget.
    pub async fn remove_network(
        &self,
        target_ssid: Vec<u8>,
        type_: fidl_policy::SecurityType,
        credential: fidl_policy::Credential,
    ) -> Result<(), Error> {
        let controller_guard = self.controller.read();
        let controller = controller_guard
            .inner
            .as_ref()
            .ok_or(format_err!("client controller has not been initialized"))?;
        fx_log_info!(
            tag: &with_line!("WlanPolicyFacade::remove_network"),
            "Removing network: ({}{:?})",
            String::from_utf8_lossy(&target_ssid),
            type_
        );

        let config = fidl_policy::NetworkConfig {
            id: Some(fidl_policy::NetworkIdentifier { ssid: target_ssid, type_ }),
            credential: Some(credential),
        };
        controller
            .remove_network(config)
            .await
            .map_err(|err| format_err!("{:?}", err))? // FIDL error
            .map_err(|err| format_err!("{:?}", err)) // network config change error
    }

    /// Remove all of the client's saved networks.
    pub async fn remove_all_networks(&self) -> Result<(), Error> {
        let controller_guard = self.controller.read();
        let controller = controller_guard
            .inner
            .as_ref()
            .ok_or(format_err!("client controller has not been initialized"))?;

        // Remove each saved network individually.
        let saved_networks = self.get_saved_networks().await?;
        for network_config in saved_networks {
            controller
                .remove_network(network_config)
                .await
                .map_err(|err| format_err!("{:?}", err))? // FIDL error
                .map_err(|err| format_err!("{:?}", err))?; // network config change error
        }
        Ok(())
    }

    /// Send the request to the policy layer to start making client connections.
    pub async fn start_client_connections(&self) -> Result<(), Error> {
        let controller_guard = self.controller.read();
        let controller = controller_guard
            .inner
            .as_ref()
            .ok_or(format_err!("client controller has not been initialized"))?;

        let req_status = controller.start_client_connections().await?;
        if fidl_common::RequestStatus::Acknowledged == req_status {
            Ok(())
        } else {
            bail!("{:?}", req_status);
        }
    }

    /// Wait for and return a client update. If this is the first update gotten from the facade
    /// since the client controller or a new update listener has been created, it will get an
    /// immediate status. After that, it will wait for a change and return a status when there has
    /// been a change since the last call to get_update. This call will hang if there are no
    /// updates.
    /// This function is not thread safe, so there should not be multiple get_update calls at the
    /// same time unless a new listener is set between them. There is no lock around the
    /// update_listener field of the facade in order to prevent a hanging get_update from blocking
    /// all future get_updates.
    pub async fn get_update(&self) -> Result<ClientStateSummary, Error> {
        // Initialize the update listener if it has not been initialized.
        let listener = self.update_listener.take();
        let mut update_listener = if listener.is_none() {
            Self::init_listener()
        } else {
            listener.ok_or(format_err!("failed to set update listener of facade"))
        }?;

        if let Some(update_request) = update_listener.try_next().await? {
            let update = update_request.into_on_client_state_update();
            let (update, responder) = match update {
                Some((update, responder)) => (update, responder),
                None => return Err(format_err!("Client provider produced invalid update.")),
            };
            // Ack the update.
            responder.send().map_err(|e| format_err!("failed to ack update: {}", e))?;
            // Put the update listener back in the facade
            self.update_listener.set(Some(update_listener));
            Ok(update.into())
        } else {
            self.update_listener.set(Some(update_listener));
            Err(format_err!("update listener's next update is None"))
        }
    }

    /// Send the request to the policy layer to stop making client connections.
    pub async fn stop_client_connections(&self) -> Result<(), Error> {
        let controller_guard = self.controller.read();
        let controller = controller_guard
            .inner
            .as_ref()
            .ok_or(format_err!("client controller has not been initialized"))?;

        let req_status = controller.stop_client_connections().await?;
        if fidl_common::RequestStatus::Acknowledged == req_status {
            Ok(())
        } else {
            bail!("{:?}", req_status);
        }
    }

    /// Save the specified network.
    /// # Arguments:
    /// * `target_ssid`:  The SSID (network name) that we want to save.
    /// * `type`: the security type of the network. It should be a string, either "none", "wep",
    ///           "wpa", "wpa2" or "wpa3", matching the policy API's defined security types. Target
    ///           password can be password, PSK, or none, represented by empty string
    /// * `credential`: the password or other credential of the network we want to remember.
    pub async fn save_network(
        &self,
        target_ssid: Vec<u8>,
        type_: fidl_policy::SecurityType,
        credential: fidl_policy::Credential,
    ) -> Result<(), Error> {
        let controller_guard = self.controller.read();
        let controller = controller_guard
            .inner
            .as_ref()
            .ok_or(format_err!("client controller has not been initialized"))?;

        let network_id = fidl_policy::NetworkIdentifier { ssid: target_ssid.clone(), type_: type_ };

        controller
            .save_network(fidl_policy::NetworkConfig {
                id: Some(network_id),
                credential: Some(credential),
            })
            .await?
            .map_err(|e| format_err!("{:?}", e))
    }

    pub async fn get_saved_networks_json(&self) -> Result<Vec<NetworkConfig>, Error> {
        let saved_networks = self.get_saved_networks().await?;
        // Convert FIDL network configs to JSON values that can be passed through SL4F
        Ok(saved_networks.into_iter().map(|cfg| cfg.into()).collect::<Vec<_>>())
    }

    /// Get a list of the saved networks. Returns FIDL values to be used directly or converted to
    /// serializable values that can be passed through SL4F
    async fn get_saved_networks(&self) -> Result<Vec<fidl_policy::NetworkConfig>, Error> {
        let controller_guard = self.controller.read();
        let controller = controller_guard
            .inner
            .as_ref()
            .ok_or(format_err!("client controller has not been initialized"))?;

        // Policy will send configs back through this iterator
        let (iter, server) =
            fidl::endpoints::create_proxy::<fidl_policy::NetworkConfigIteratorMarker>()
                .context("failed to create iterator")?;
        controller
            .get_saved_networks(server)
            .map_err(|e| format_err!("Get saved networks: fidl error {:?}", e))?;

        // get each config from the stream as they become available
        let mut networks = vec![];
        loop {
            let cfgs = iter.get_next().await?;
            if cfgs.is_empty() {
                break;
            }
            networks.extend(cfgs);
        }
        Ok(networks)
    }

    fn stringify_scan_results(results: Vec<fidl_policy::ScanResult>) -> Vec<String> {
        results
            .into_iter()
            .filter_map(|result| result.id)
            .map(|id| String::from_utf8_lossy(&id.ssid).into_owned())
            .collect()
    }
}
