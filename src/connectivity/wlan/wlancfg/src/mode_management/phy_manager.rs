// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        mode_management::iface_manager::wpa3_in_features, regulatory_manager::REGION_CODE_LEN,
    },
    async_trait::async_trait,
    eui48::MacAddress,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_device as fidl_device,
    fidl_fuchsia_wlan_device::MacRole,
    fidl_fuchsia_wlan_device_service as fidl_service,
    fuchsia_inspect::{self as inspect, NumericProperty},
    fuchsia_zircon,
    log::{error, info, warn},
    std::collections::{HashMap, HashSet},
    thiserror::Error,
};

/// Errors raised while attempting to query information about or configure PHYs and ifaces.
#[derive(Debug, Error)]
pub enum PhyManagerError {
    #[error("the requested operation is not supported")]
    Unsupported,
    #[error("unable to query phy information")]
    PhyQueryFailure,
    #[error("failed to set country for new PHY")]
    PhySetCountryFailure,
    #[error("unable to query iface information")]
    IfaceQueryFailure,
    #[error("unable to create iface")]
    IfaceCreateFailure,
    #[error("unable to destroy iface")]
    IfaceDestroyFailure,
}

/// There are a variety of reasons why the calling code may want to create client interfaces.  The
/// main logic to do so is identical, but there are different intents for making the call.  This
/// enum allows callers to express their intent when making the call to ensure that internal
/// PhyManager state remains consistent with the current desired mode of operation.
#[derive(PartialEq)]
pub enum CreateClientIfacesReason {
    StartClientConnections,
    RecoverClientIfaces,
}

/// Stores information about a WLAN PHY and any interfaces that belong to it.
pub(crate) struct PhyContainer {
    supported_mac_roles: Vec<fidl_device::MacRole>,
    // Driver features are tracked for each interface so that callers can request an interface
    // based on capabilities.
    client_ifaces: HashMap<u16, Vec<fidl_common::DriverFeature>>,
    ap_ifaces: HashSet<u16>,
}

#[async_trait]
pub trait PhyManagerApi {
    /// Checks to see if this PHY is already accounted for.  If it is not, queries its PHY
    /// attributes and places it in the hash map.
    async fn add_phy(&mut self, phy_id: u16) -> Result<(), PhyManagerError>;

    /// If the PHY is accounted for, removes the associated `PhyContainer` from the hash map.
    fn remove_phy(&mut self, phy_id: u16);

    /// Queries the interface properties to get the PHY ID.  If the `PhyContainer`
    /// representing the interface's parent PHY is already present and its
    /// interface information is obsolete, updates it.  The PhyManager will track ifaces
    /// as it creates and deletes them, but it is possible that another caller circumvents the
    /// policy layer and creates an interface.  If no `PhyContainer` exists
    /// for the new interface, creates one and adds the newly discovered interface
    /// ID to it.
    async fn on_iface_added(&mut self, iface_id: u16) -> Result<(), PhyManagerError>;

    /// Ensures that the `iface_id` is not present in any of the `PhyContainer` interface lists.
    fn on_iface_removed(&mut self, iface_id: u16);

    /// Creates client interfaces for all PHYs that are capable of acting as clients.  For newly
    /// discovered PHYs, create client interfaces if the PHY can support them.  This method returns
    /// a vector containing all newly-created client interface IDs along with a representation of
    /// any errors encountered along the way.
    async fn create_all_client_ifaces(
        &mut self,
        reason: CreateClientIfacesReason,
    ) -> Result<Vec<u16>, (Vec<u16>, PhyManagerError)>;

    /// The PhyManager is the authoritative source of whether or not the policy layer is allowed to
    /// create client interfaces.  This method allows other parts of the policy layer to determine
    /// whether the API client has allowed client interfaces to be created.
    fn client_connections_enabled(&self) -> bool;

    /// Destroys all client interfaces.  Do not allow the creation of client interfaces for newly
    /// discovered PHYs.
    async fn destroy_all_client_ifaces(&mut self) -> Result<(), PhyManagerError>;

    /// Finds a PHY with a client interface and returns the interface's ID to the caller.
    fn get_client(&mut self) -> Option<u16>;

    /// Finds a WPA3 capable PHY with a client interface and returns the interface's ID.
    fn get_wpa3_capable_client(&mut self) -> Option<u16>;

    /// Finds a PHY that is capable of functioning as an AP.  PHYs that do not yet have AP ifaces
    /// associated with them are searched first.  If one is found, an AP iface is created and its
    /// ID is returned.  If all AP-capable PHYs already have AP ifaces associated with them, one of
    /// the existing AP iface IDs is returned.  If there are no AP-capable PHYs, None is returned.
    async fn create_or_get_ap_iface(&mut self) -> Result<Option<u16>, PhyManagerError>;

    /// Destroys the interface associated with the given interface ID.
    async fn destroy_ap_iface(&mut self, iface_id: u16) -> Result<(), PhyManagerError>;

    /// Destroys all AP interfaces.
    async fn destroy_all_ap_ifaces(&mut self) -> Result<(), PhyManagerError>;

    /// Sets a suggested MAC address to be used by new AP interfaces.
    fn suggest_ap_mac(&mut self, mac: MacAddress);

    /// Returns the IDs for all currently known PHYs.
    fn get_phy_ids(&self) -> Vec<u16>;

    /// Logs phy add failure inspect metrics.
    fn log_phy_add_failure(&mut self);

    /// Sets the country code on all known PHYs and stores the country code to be applied to
    /// newly-discovered PHYs.
    async fn set_country_code(
        &mut self,
        country_code: Option<[u8; REGION_CODE_LEN]>,
    ) -> Result<(), PhyManagerError>;

    /// Returns whether any PHY has a client interface that supports WPA3.
    fn has_wpa3_client_iface(&self) -> bool;
}

/// Maintains a record of all PHYs that are present and their associated interfaces.
pub struct PhyManager {
    phys: HashMap<u16, PhyContainer>,
    device_service: fidl_service::DeviceServiceProxy,
    device_monitor: fidl_service::DeviceMonitorProxy,
    client_connections_enabled: bool,
    suggested_ap_mac: Option<MacAddress>,
    saved_country_code: Option<[u8; REGION_CODE_LEN]>,
    _node: inspect::Node,
    phy_add_fail_count: inspect::UintProperty,
}

impl PhyContainer {
    /// Stores the PhyInfo associated with a newly discovered PHY and creates empty vectors to hold
    /// interface IDs that belong to this PHY.
    pub fn new(supported_mac_roles: Vec<fidl_device::MacRole>) -> Self {
        PhyContainer {
            supported_mac_roles,
            client_ifaces: HashMap::new(),
            ap_ifaces: HashSet::new(),
        }
    }
}

// TODO(fxbug.dev/49590): PhyManager makes the assumption that WLAN PHYs that support client and AP modes can
// can operate as clients and APs simultaneously.  For PHYs where this is not the case, the
// existing interface should be destroyed before the new interface is created.
impl PhyManager {
    /// Internally stores a DeviceMonitorProxy to query PHY and interface properties and create and
    /// destroy interfaces as requested.
    pub fn new(
        device_service: fidl_service::DeviceServiceProxy,
        device_monitor: fidl_service::DeviceMonitorProxy,
        node: inspect::Node,
    ) -> Self {
        let phy_add_fail_count = node.create_uint("phy_add_fail_count", 0);
        PhyManager {
            phys: HashMap::new(),
            device_service,
            device_monitor,
            client_connections_enabled: false,
            suggested_ap_mac: None,
            saved_country_code: None,
            _node: node,
            phy_add_fail_count,
        }
    }
    /// Verifies that a given PHY ID is accounted for and, if not, adds a new entry for it.
    async fn ensure_phy(&mut self, phy_id: u16) -> Result<&mut PhyContainer, PhyManagerError> {
        if !self.phys.contains_key(&phy_id) {
            self.add_phy(phy_id).await?;
        }

        // The phy_id is guaranteed to exist at this point because it was either previously
        // accounted for or was just added above.
        Ok(self.phys.get_mut(&phy_id).unwrap())
    }

    /// Queries the information associated with the given iface ID.
    async fn query_iface(
        &self,
        iface_id: u16,
    ) -> Result<Option<fidl_service::QueryIfaceResponse>, PhyManagerError> {
        match self.device_service.query_iface(iface_id).await {
            Ok((status, response)) => match status {
                fuchsia_zircon::sys::ZX_OK => match response {
                    Some(response) => Ok(Some(*response)),
                    None => Ok(None),
                },
                fuchsia_zircon::sys::ZX_ERR_NOT_FOUND => Ok(None),
                _ => Err(PhyManagerError::IfaceQueryFailure),
            },
            Err(_) => Err(PhyManagerError::IfaceQueryFailure),
        }
    }

    /// Returns a list of PHY IDs that can have interfaces of the requested MAC role.
    fn phys_for_role(&self, role: MacRole) -> Vec<u16> {
        self.phys
            .iter()
            .filter_map(
                |(k, v)| {
                    if v.supported_mac_roles.contains(&role) {
                        Some(*k)
                    } else {
                        None
                    }
                },
            )
            .collect()
    }

    async fn get_iface_driver_features(
        &self,
        iface_id: u16,
    ) -> Result<Vec<fidl_common::DriverFeature>, PhyManagerError> {
        // Get the driver features for this iface.
        let (status, iface_info) = self
            .device_service
            .query_iface(iface_id)
            .await
            .map_err(|_| PhyManagerError::IfaceQueryFailure)?;
        fuchsia_zircon::ok(status).map_err(|_| PhyManagerError::IfaceQueryFailure)?;
        let iface_info = iface_info.ok_or_else(|| PhyManagerError::IfaceQueryFailure)?;
        Ok(iface_info.driver_features)
    }
}

#[async_trait]
impl PhyManagerApi for PhyManager {
    async fn add_phy(&mut self, phy_id: u16) -> Result<(), PhyManagerError> {
        let mac_roles_response =
            self.device_monitor.get_supported_mac_roles(phy_id).await.map_err(|e| {
                warn!("Failed to communicate with monitor service: {:?}", e);
                PhyManagerError::PhyQueryFailure
            })?;
        let mac_roles = mac_roles_response.ok_or(PhyManagerError::PhyQueryFailure)?;

        // Create a new container to store the PHY's information.
        info!("adding PHY ID #{}", phy_id);
        let mut phy_container = PhyContainer::new(mac_roles);

        // Attempt to set the country for the newly-discovered PHY.
        let set_country_result = match self.saved_country_code {
            Some(country_code) => {
                set_phy_country_code(&self.device_monitor, phy_id, country_code).await
            }
            None => Ok(()),
        };

        // If setting the country code fails, clear the PHY's country code so that it is in WW
        // and can continue to operate.  If this process fails, return early and do not use this
        // PHY.
        if set_country_result.is_err() {
            clear_phy_country_code(&self.device_monitor, phy_id).await?;
        }

        if self.client_connections_enabled
            && phy_container.supported_mac_roles.contains(&MacRole::Client)
        {
            let iface_id =
                create_iface(&self.device_monitor, phy_id, MacRole::Client, None).await?;
            // Find out the capabilities of the iface.
            let driver_features = self.get_iface_driver_features(iface_id).await?;
            if let Some(_) = phy_container.client_ifaces.insert(iface_id, driver_features) {
                warn!("Unexpectedly replaced existing iface information for id {}", iface_id);
            };
        }

        if let Some(_) = self.phys.insert(phy_id, phy_container) {
            warn!("Unexpectedly replaced existing phy information for id {}", phy_id);
        };

        Ok(())
    }

    fn remove_phy(&mut self, phy_id: u16) {
        if self.phys.remove(&phy_id).is_none() {
            warn!("Attempted to remove non-existed phy {}", phy_id);
        };
    }

    async fn on_iface_added(&mut self, iface_id: u16) -> Result<(), PhyManagerError> {
        if let Some(query_iface_response) = self.query_iface(iface_id).await? {
            let phy = self.ensure_phy(query_iface_response.phy_id).await?;
            let iface_id = query_iface_response.id;
            let driver_features = query_iface_response.driver_features;

            match query_iface_response.role {
                MacRole::Client => {
                    if let Some(old_driver_features) =
                        phy.client_ifaces.insert(iface_id, driver_features.clone())
                    {
                        if old_driver_features != driver_features {
                            warn!("Driver features changed unexpectedly for id {}", iface_id);
                        }
                    } else {
                        // The iface wasn't in the hashmap, so it was created by someone else
                        warn!("Detected an unexpected client iface id {} created outside of PhyManager", iface_id);
                    }
                }
                MacRole::Ap => {
                    if phy.ap_ifaces.insert(iface_id) {
                        // `.insert()` returns true if the value was not already present
                        warn!("Detected an unexpected AP iface created outside of PhyManager");
                    }
                }
                MacRole::Mesh => {
                    return Err(PhyManagerError::Unsupported);
                }
            }
        }
        Ok(())
    }

    fn on_iface_removed(&mut self, iface_id: u16) {
        for (_, phy_info) in self.phys.iter_mut() {
            if phy_info.client_ifaces.remove(&iface_id).is_none()
                && !phy_info.ap_ifaces.remove(&iface_id)
            {
                warn!(
                    "Attempted to remove iface id {} but it was not in client or ap iface list",
                    iface_id
                );
            };
        }
    }

    async fn create_all_client_ifaces(
        &mut self,
        reason: CreateClientIfacesReason,
    ) -> Result<Vec<u16>, (Vec<u16>, PhyManagerError)> {
        if reason == CreateClientIfacesReason::StartClientConnections {
            self.client_connections_enabled = true;
        }

        let mut recovered_iface_ids = Vec::new();
        let mut error_encountered = Ok(());

        if self.client_connections_enabled {
            let client_capable_phy_ids = self.phys_for_role(MacRole::Client);

            for client_phy in client_capable_phy_ids.iter() {
                let phy_container = match self.phys.get_mut(&client_phy) {
                    Some(phy_container) => phy_container,
                    None => {
                        error_encountered = Err(PhyManagerError::PhyQueryFailure);
                        continue;
                    }
                };

                // If a PHY should be able to have a client interface and it does not, create a new
                // client interface for the PHY.
                if phy_container.client_ifaces.is_empty() {
                    let iface_id = match create_iface(
                        &self.device_monitor,
                        *client_phy,
                        MacRole::Client,
                        None,
                    )
                    .await
                    {
                        Ok(iface_id) => iface_id,
                        Err(e) => {
                            warn!("Failed to recover iface for PHY {}: {:?}", client_phy, e);
                            error_encountered = Err(e);
                            continue;
                        }
                    };
                    let driver_features = get_iface_driver_features(&self.device_service, iface_id)
                        .await
                        .unwrap_or_else(|e| {
                            error!("Error occurred getting iface driver features: {}", e);
                            Vec::new()
                        });
                    let _ = phy_container.client_ifaces.insert(iface_id, driver_features);

                    recovered_iface_ids.push(iface_id);
                }
            }
        }

        match error_encountered {
            Ok(()) => Ok(recovered_iface_ids),
            Err(e) => Err((recovered_iface_ids, e)),
        }
    }

    fn client_connections_enabled(&self) -> bool {
        self.client_connections_enabled
    }

    async fn destroy_all_client_ifaces(&mut self) -> Result<(), PhyManagerError> {
        self.client_connections_enabled = false;

        let client_capable_phys = self.phys_for_role(MacRole::Client);
        let mut result = Ok(());

        for client_phy in client_capable_phys.iter() {
            let phy_container =
                self.phys.get_mut(&client_phy).ok_or(PhyManagerError::PhyQueryFailure)?;

            // Continue tracking interface IDs for which deletion fails.
            let mut lingering_ifaces = HashMap::new();

            for (iface_id, driver_features) in phy_container.client_ifaces.drain() {
                match destroy_iface(&self.device_monitor, iface_id).await {
                    Ok(()) => {}
                    Err(e) => {
                        result = Err(e);
                        if let Some(_) = lingering_ifaces.insert(iface_id, driver_features) {
                            warn!("Unexpected duplicate lingering iface for id {}", iface_id);
                        };
                    }
                }
            }
            phy_container.client_ifaces = lingering_ifaces;
        }

        result
    }

    fn get_client(&mut self) -> Option<u16> {
        let client_capable_phys = self.phys_for_role(MacRole::Client);
        if client_capable_phys.is_empty() {
            return None;
        }

        // Find the first PHY with any client interfaces and return its first client interface.
        let phy = self.phys.get_mut(&client_capable_phys[0])?;
        match phy.client_ifaces.keys().next() {
            Some(iface_id) => Some(*iface_id),
            None => None,
        }
    }

    fn get_wpa3_capable_client(&mut self) -> Option<u16> {
        // Search phys for a client iface with the driver feature indicating WPA3 support.
        for (_, phy) in self.phys.iter() {
            for (iface_id, driver_features) in phy.client_ifaces.iter() {
                if wpa3_in_features(&driver_features) {
                    return Some(*iface_id);
                }
            }
        }
        None
    }

    async fn create_or_get_ap_iface(&mut self) -> Result<Option<u16>, PhyManagerError> {
        let ap_capable_phy_ids = self.phys_for_role(MacRole::Ap);
        if ap_capable_phy_ids.is_empty() {
            return Ok(None);
        }

        // First check for any PHYs that can have AP interfaces but do not yet
        for ap_phy_id in ap_capable_phy_ids.iter() {
            let phy_container =
                self.phys.get_mut(&ap_phy_id).ok_or(PhyManagerError::PhyQueryFailure)?;
            if phy_container.ap_ifaces.is_empty() {
                let mac = match self.suggested_ap_mac {
                    Some(mac) => Some(mac.as_bytes().to_vec()),
                    None => None,
                };
                let iface_id =
                    create_iface(&self.device_monitor, *ap_phy_id, MacRole::Ap, mac).await?;

                let _ = phy_container.ap_ifaces.insert(iface_id);
                return Ok(Some(iface_id));
            }
        }

        // If all of the AP-capable PHYs have created AP interfaces already, return the
        // first observed existing AP interface
        // TODO(fxbug.dev/49843): Figure out a better method of interface selection.
        let phy = match self.phys.get_mut(&ap_capable_phy_ids[0]) {
            Some(phy_container) => phy_container,
            None => return Ok(None),
        };
        match phy.ap_ifaces.iter().next() {
            Some(iface_id) => Ok(Some(*iface_id)),
            None => Ok(None),
        }
    }

    async fn destroy_ap_iface(&mut self, iface_id: u16) -> Result<(), PhyManagerError> {
        // If the interface has already been destroyed, return Ok.  Only error out in the case that
        // the request to destroy the interface results in a failure.
        for (_, phy_container) in self.phys.iter_mut() {
            if phy_container.ap_ifaces.remove(&iface_id) {
                match destroy_iface(&self.device_monitor, iface_id).await {
                    Ok(()) => {}
                    Err(e) => {
                        let _ = phy_container.ap_ifaces.insert(iface_id);
                        return Err(e);
                    }
                }
            }
        }
        Ok(())
    }

    async fn destroy_all_ap_ifaces(&mut self) -> Result<(), PhyManagerError> {
        let ap_capable_phys = self.phys_for_role(MacRole::Ap);
        let mut result = Ok(());

        for ap_phy in ap_capable_phys.iter() {
            let phy_container =
                self.phys.get_mut(&ap_phy).ok_or(PhyManagerError::PhyQueryFailure)?;

            // Continue tracking interface IDs for which deletion fails.
            let mut lingering_ifaces = HashSet::new();
            for iface_id in phy_container.ap_ifaces.drain() {
                match destroy_iface(&self.device_monitor, iface_id).await {
                    Ok(()) => {}
                    Err(e) => {
                        result = Err(e);
                        let _ = lingering_ifaces.insert(iface_id);
                    }
                }
            }
            phy_container.ap_ifaces = lingering_ifaces;
        }
        result
    }

    fn suggest_ap_mac(&mut self, mac: MacAddress) {
        self.suggested_ap_mac = Some(mac);
    }

    fn get_phy_ids(&self) -> Vec<u16> {
        self.phys.keys().cloned().collect()
    }

    fn log_phy_add_failure(&mut self) {
        self.phy_add_fail_count.add(1);
    }

    async fn set_country_code(
        &mut self,
        country_code: Option<[u8; REGION_CODE_LEN]>,
    ) -> Result<(), PhyManagerError> {
        self.saved_country_code = country_code;

        match country_code {
            Some(country_code) => {
                for phy_id in self.phys.keys() {
                    set_phy_country_code(&self.device_monitor, *phy_id, country_code).await?;
                }
            }
            None => {
                for phy_id in self.phys.keys() {
                    clear_phy_country_code(&self.device_monitor, *phy_id).await?;
                }
            }
        }

        Ok(())
    }

    // Returns whether at least one of phy's iface supports WPA3.
    fn has_wpa3_client_iface(&self) -> bool {
        // Search phys for a client iface with the driver feature indicating WPA3 support.
        for (_, phy) in self.phys.iter() {
            for (_, driver_features) in phy.client_ifaces.iter() {
                if wpa3_in_features(driver_features) {
                    return true;
                }
            }
        }
        false
    }
}

/// Creates an interface of the requested role for the requested PHY ID.  Returns either the
/// ID of the created interface or an error.
async fn create_iface(
    proxy: &fidl_service::DeviceMonitorProxy,
    phy_id: u16,
    role: MacRole,
    mac: Option<Vec<u8>>,
) -> Result<u16, PhyManagerError> {
    let mut request = fidl_service::CreateIfaceRequest { phy_id, role, mac_addr: mac };
    let create_iface_response = match proxy.create_iface(&mut request).await {
        Ok((status, iface_response)) => {
            if fuchsia_zircon::ok(status).is_err() || iface_response.is_none() {
                return Err(PhyManagerError::IfaceCreateFailure);
            }
            iface_response.ok_or_else(|| PhyManagerError::IfaceCreateFailure)?
        }
        Err(e) => {
            warn!("failed to create iface for PHY {}: {}", phy_id, e);
            return Err(PhyManagerError::IfaceCreateFailure);
        }
    };
    Ok(create_iface_response.iface_id)
}

/// Destroys the specified interface.
async fn destroy_iface(
    proxy: &fidl_service::DeviceMonitorProxy,
    iface_id: u16,
) -> Result<(), PhyManagerError> {
    let mut request = fidl_service::DestroyIfaceRequest { iface_id: iface_id };
    match proxy.destroy_iface(&mut request).await {
        Ok(status) => match status {
            fuchsia_zircon::sys::ZX_OK => Ok(()),
            ref e => {
                warn!("failed to destroy iface {}: {}", iface_id, e);
                Err(PhyManagerError::IfaceDestroyFailure)
            }
        },
        Err(e) => {
            warn!("failed to send destroy iface {}: {}", iface_id, e);
            Err(PhyManagerError::IfaceDestroyFailure)
        }
    }
}

async fn get_iface_driver_features(
    proxy: &fidl_service::DeviceServiceProxy,
    iface_id: u16,
) -> Result<Vec<fidl_common::DriverFeature>, PhyManagerError> {
    // Get the driver features for this iface.
    let (status, iface_info) =
        proxy.query_iface(iface_id).await.map_err(|_| PhyManagerError::IfaceQueryFailure)?;
    fuchsia_zircon::ok(status).map_err(|_| PhyManagerError::IfaceQueryFailure)?;
    let iface_info = iface_info.ok_or_else(|| PhyManagerError::IfaceQueryFailure)?;
    Ok(iface_info.driver_features)
}

async fn set_phy_country_code(
    proxy: &fidl_service::DeviceMonitorProxy,
    phy_id: u16,
    country_code: [u8; REGION_CODE_LEN],
) -> Result<(), PhyManagerError> {
    let status = proxy
        .set_country(&mut fidl_service::SetCountryRequest { phy_id, alpha2: country_code })
        .await
        .map_err(|e| {
            error!("Failed to set country code for PHY {}: {:?}", phy_id, e);
            PhyManagerError::PhySetCountryFailure
        })?;

    fuchsia_zircon::ok(status).map_err(|e| {
        error!("Received bad status when setting country code for PHY {}: {}", phy_id, e);
        PhyManagerError::PhySetCountryFailure
    })
}

async fn clear_phy_country_code(
    proxy: &fidl_service::DeviceMonitorProxy,
    phy_id: u16,
) -> Result<(), PhyManagerError> {
    let status = proxy
        .clear_country(&mut fidl_service::ClearCountryRequest { phy_id })
        .await
        .map_err(|e| {
            error!("Failed to clear country code for PHY {}: {:?}", phy_id, e);
            PhyManagerError::PhySetCountryFailure
        })?;

    fuchsia_zircon::ok(status).map_err(|e| {
        error!("Received bad status when clearing country code for PHY {}: {}", phy_id, e);
        PhyManagerError::PhySetCountryFailure
    })
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints,
        fidl_fuchsia_wlan_device as fidl_device, fidl_fuchsia_wlan_device_service as fidl_service,
        fuchsia_async::{run_singlethreaded, TestExecutor},
        fuchsia_inspect::{self as inspect, assert_data_tree},
        fuchsia_zircon::sys::{ZX_ERR_NOT_FOUND, ZX_OK},
        futures::stream::StreamExt,
        futures::task::Poll,
        pin_utils::pin_mut,
        test_case::test_case,
        wlan_common::assert_variant,
    };

    /// Hold the client and service ends for DeviceMonitor to allow mocking DeviceMonitor responses
    /// for unit tests.
    struct TestValues {
        dev_svc_proxy: fidl_service::DeviceServiceProxy,
        dev_svc_stream: fidl_service::DeviceServiceRequestStream,
        monitor_proxy: fidl_service::DeviceMonitorProxy,
        monitor_stream: fidl_service::DeviceMonitorRequestStream,
        inspector: inspect::Inspector,
        node: inspect::Node,
    }

    /// Create a TestValues for a unit test.
    fn test_setup() -> TestValues {
        let (dev_svc_proxy, dev_svc_requests) =
            endpoints::create_proxy::<fidl_service::DeviceServiceMarker>()
                .expect("failed to create DeviceService proxy");
        let dev_svc_stream = dev_svc_requests.into_stream().expect("failed to create stream");

        let (monitor_proxy, monitor_requests) =
            endpoints::create_proxy::<fidl_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let monitor_stream = monitor_requests.into_stream().expect("failed to create stream");

        let inspector = inspect::Inspector::new();
        let node = inspector.root().create_child("phy_manager");

        TestValues { dev_svc_proxy, dev_svc_stream, monitor_proxy, monitor_stream, inspector, node }
    }

    /// Take in the service side of a DeviceMonitor::GetSupportedMacRoles request and respond with
    /// the given MacRoles responst.
    fn send_get_supported_mac_roles_response(
        exec: &mut TestExecutor,
        server: &mut fidl_service::DeviceMonitorRequestStream,
        supported_mac_roles: Option<Vec<fidl_device::MacRole>>,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut server.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceMonitorRequest::GetSupportedMacRoles {
                    responder, ..
                }
            ))) => {
                match supported_mac_roles {
                    Some(mut supported_mac_roles) => {
                        responder.send(Some(&mut supported_mac_roles.drain(..)))
                            .expect("sending fake MAC roles");
                    }
                    None => responder.send(None).expect("sending fake response with none")
                }
            }
        );
    }

    /// Create a PhyInfo object for unit testing.
    #[track_caller]
    fn send_query_iface_response(
        exec: &mut TestExecutor,
        server: &mut fidl_service::DeviceServiceRequestStream,
        iface_info: Option<&mut fidl_service::QueryIfaceResponse>,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut server.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceServiceRequest::QueryIface {
                    iface_id: _,
                    responder,
                }
            ))) => {
                responder.send(ZX_OK, iface_info).expect("sending fake iface info");
            }
        );
    }

    /// Handles the service side of a DeviceMonitor::CreateIface request by replying with the
    /// provided optional iface ID.
    #[track_caller]
    fn send_create_iface_response(
        exec: &mut TestExecutor,
        server: &mut fidl_service::DeviceMonitorRequestStream,
        iface_id: Option<u16>,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut server.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceMonitorRequest::CreateIface {
                    req: _,
                    responder,
                }
            ))) => {
                match iface_id {
                    Some(iface_id) => responder.send(
                        ZX_OK,
                        Some(&mut fidl_service::CreateIfaceResponse {
                            iface_id: iface_id,
                        })
                    )
                    .expect("sending fake iface id"),
                    None => responder.send(ZX_ERR_NOT_FOUND, None).expect("sending fake response with none")
                }
            }
        );
    }

    /// Handles the service side of a DeviceMonitor::DestroyIface request by replying with the
    /// provided zx_status_t.
    fn send_destroy_iface_response(
        exec: &mut TestExecutor,
        server: &mut fidl_service::DeviceMonitorRequestStream,
        return_status: fuchsia_zircon::zx_status_t,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut server.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceMonitorRequest::DestroyIface {
                    req: _,
                    responder,
                }
            ))) => {
                responder
                    .send(return_status)
                    .expect(format!("sending fake response: {}", return_status).as_str());
            }
        );
    }

    /// Create a PhyInfo object for unit testing.
    fn fake_phy_info(id: u16, mac_roles: Vec<MacRole>) -> fidl_device::PhyInfo {
        fidl_device::PhyInfo { id: id, dev_path: None, supported_mac_roles: mac_roles }
    }

    /// Creates a QueryIfaceResponse from the arguments provided by the caller.
    fn create_iface_response(
        role: MacRole,
        id: u16,
        phy_id: u16,
        phy_assigned_id: u16,
        mac: [u8; 6],
        driver_features: Vec<fidl_common::DriverFeature>,
    ) -> fidl_service::QueryIfaceResponse {
        fidl_service::QueryIfaceResponse {
            role: role,
            id: id,
            phy_id: phy_id,
            phy_assigned_id: phy_assigned_id,
            mac_addr: mac,
            driver_features,
        }
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnPhyCreated event and
    /// calling add_phy on PhyManager for a PHY that exists.  The expectation is that the
    /// PhyManager initially does not have any PHYs available.  After the call to add_phy, the
    /// PhyManager should have a new PhyContainer.
    #[fuchsia::test]
    fn add_valid_phy() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        let fake_phy_id = 0;
        let fake_mac_roles = Vec::new();

        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);
        {
            let add_phy_fut = phy_manager.add_phy(0);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Some(fake_mac_roles.clone()),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));
        assert_eq!(phy_manager.phys.get(&fake_phy_id).unwrap().supported_mac_roles, fake_mac_roles);
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnPhyCreated event and
    /// calling add_phy on PhyManager for a PHY that does not exist.  The PhyManager in this case
    /// should not create and store a new PhyContainer.
    #[fuchsia::test]
    fn add_invalid_phy() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        {
            let add_phy_fut = phy_manager.add_phy(1);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(&mut exec, &mut test_values.monitor_stream, None);

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }
        assert!(phy_manager.phys.is_empty());
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnPhyCreated event and
    /// calling add_phy on PhyManager for a PHY that has already been accounted for, but whose
    /// properties have changed.  The PhyManager in this case should update the associated PhyInfo.
    #[fuchsia::test]
    fn add_duplicate_phy() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        let fake_phy_id = 0;
        let fake_mac_roles = Vec::new();

        {
            let add_phy_fut = phy_manager.add_phy(fake_phy_id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Some(fake_mac_roles.clone()),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        {
            assert!(phy_manager.phys.contains_key(&fake_phy_id));
            assert_eq!(
                phy_manager.phys.get(&fake_phy_id).unwrap().supported_mac_roles,
                fake_mac_roles
            );
        }

        // Send an update for the same PHY ID and ensure that the PHY info is updated.
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles.clone());

        {
            let add_phy_fut = phy_manager.add_phy(phy_info.id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Some(fake_mac_roles.clone()),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));
        assert_eq!(phy_manager.phys.get(&fake_phy_id).unwrap().supported_mac_roles, fake_mac_roles);
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnPhyRemoved event and
    /// calling remove_phy on PhyManager for a PHY that not longer exists.  The PhyManager in this
    /// case should remove the PhyContainer associated with the removed PHY ID.
    #[fuchsia::test]
    fn add_phy_after_create_all_client_ifaces() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Client];
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles.clone());

        let fake_phy_assigned_id = 0;
        let fake_mac_addr = [0, 0, 0, 0, 0, 0];
        let driver_features = vec![fidl_common::DriverFeature::SaeDriverAuth];
        let mut iface_response = create_iface_response(
            fake_mac_roles[0],
            fake_iface_id,
            fake_phy_id,
            fake_phy_assigned_id,
            fake_mac_addr,
            driver_features.clone(),
        );

        {
            let start_connections_fut = phy_manager
                .create_all_client_ifaces(CreateClientIfacesReason::StartClientConnections);
            pin_mut!(start_connections_fut);
            assert!(exec.run_until_stalled(&mut start_connections_fut).is_ready());
        }

        // Add a new phy.  Since client connections have been started, it should also create a
        // client iface.
        {
            let add_phy_fut = phy_manager.add_phy(phy_info.id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Some(fake_mac_roles.clone()),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_create_iface_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Some(fake_iface_id),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_query_iface_response(
                &mut exec,
                &mut test_values.dev_svc_stream,
                Some(&mut iface_response),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));
        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.client_ifaces.contains_key(&fake_iface_id));
        assert_eq!(phy_container.client_ifaces.get(&fake_iface_id), Some(&driver_features));
    }

    /// Tests the case where a new PHY is discovered after the country code has been set.
    #[fuchsia::test]
    fn test_add_phy_after_setting_country_code() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles.clone());

        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        {
            let set_country_fut = phy_manager.set_country_code(Some([0, 1]));
            pin_mut!(set_country_fut);
            assert_variant!(exec.run_until_stalled(&mut set_country_fut), Poll::Ready(Ok(())));
        }

        {
            let add_phy_fut = phy_manager.add_phy(phy_info.id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Some(fake_mac_roles.clone()),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            assert_variant!(
                exec.run_until_stalled(&mut test_values.monitor_stream.next()),
                Poll::Ready(Some(Ok(
                    fidl_service::DeviceMonitorRequest::SetCountry {
                        req: fidl_service::SetCountryRequest {
                            phy_id: 1,
                            alpha2: [0, 1],
                        },
                        responder,
                    }
                ))) => {
                    responder.send(ZX_OK).expect("sending fake set country response");
                }
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));
        assert_eq!(phy_manager.phys.get(&fake_phy_id).unwrap().supported_mac_roles, fake_mac_roles);
    }

    #[run_singlethreaded(test)]
    async fn remove_valid_phy() {
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();

        let phy_container = PhyContainer::new(fake_mac_roles);
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);
        phy_manager.remove_phy(fake_phy_id);
        assert!(phy_manager.phys.is_empty());
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnPhyRemoved event and
    /// calling remove_phy on PhyManager for a PHY ID that is not accounted for by the PhyManager.
    /// The PhyManager should realize that it is unaware of this PHY ID and leave its PhyContainers
    /// unchanged.
    #[run_singlethreaded(test)]
    async fn remove_nonexistent_phy() {
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();

        let phy_container = PhyContainer::new(fake_mac_roles);
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);
        phy_manager.remove_phy(2);
        assert!(phy_manager.phys.contains_key(&fake_phy_id));
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnIfaceAdded event for
    /// an iface that belongs to a PHY that has been accounted for.  The PhyManager should add the
    /// newly discovered iface to the existing PHY's list of client ifaces.
    #[fuchsia::test]
    fn on_iface_added() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();

        let phy_container = PhyContainer::new(fake_mac_roles);

        // Create an IfaceResponse to be sent to the PhyManager when the iface ID is queried
        let fake_role = MacRole::Client;
        let fake_iface_id = 1;
        let fake_phy_assigned_id = 1;
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let driver_features = Vec::new();
        let mut iface_response = create_iface_response(
            fake_role,
            fake_iface_id,
            fake_phy_id,
            fake_phy_assigned_id,
            fake_mac_addr,
            driver_features,
        );

        {
            // Inject the fake PHY information
            let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

            // Add the fake iface
            let on_iface_added_fut = phy_manager.on_iface_added(fake_iface_id);
            pin_mut!(on_iface_added_fut);
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_query_iface_response(
                &mut exec,
                &mut test_values.dev_svc_stream,
                Some(&mut iface_response),
            );

            // Wait for the PhyManager to finish processing the received iface information
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_ready());
        }

        // Expect that the PhyContainer associated with the fake PHY has been updated with the
        // fake client
        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.client_ifaces.contains_key(&fake_iface_id));
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnIfaceAdded event for
    /// an iface that belongs to a PHY that has not been accounted for.  The PhyManager should
    /// query the PHY's information, create a new PhyContainer, and insert the new iface ID into
    /// the PHY's list of client ifaces.
    #[fuchsia::test]
    fn on_iface_added_missing_phy() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();

        // Create an IfaceResponse to be sent to the PhyManager when the iface ID is queried
        let fake_role = MacRole::Client;
        let fake_iface_id = 1;
        let fake_phy_assigned_id = 1;
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let driver_features = Vec::new();
        let mut iface_response = create_iface_response(
            fake_role,
            fake_iface_id,
            fake_phy_id,
            fake_phy_assigned_id,
            fake_mac_addr,
            driver_features,
        );

        {
            // Add the fake iface
            let on_iface_added_fut = phy_manager.on_iface_added(fake_iface_id);
            pin_mut!(on_iface_added_fut);

            // Since the PhyManager has not accounted for any PHYs, it will get the iface
            // information first and then query for the iface's PHY's information.

            // The iface query goes out first
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_query_iface_response(
                &mut exec,
                &mut test_values.dev_svc_stream,
                Some(&mut iface_response),
            );

            // And then the PHY information is queried.
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Some(fake_mac_roles),
            );

            // Wait for the PhyManager to finish processing the received iface information
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_ready());
        }

        // Expect that the PhyContainer associated with the fake PHY has been updated with the
        // fake client
        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.client_ifaces.contains_key(&fake_iface_id));
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnIfaceAdded event for
    /// an iface that was created by PhyManager and has already been accounted for.  The PhyManager
    /// should simply ignore the duplicate iface ID and not append it to its list of clients.
    #[fuchsia::test]
    fn add_duplicate_iface() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();

        // Inject the fake PHY information
        let phy_container = PhyContainer::new(fake_mac_roles);
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Create an IfaceResponse to be sent to the PhyManager when the iface ID is queried
        let fake_role = MacRole::Client;
        let fake_iface_id = 1;
        let fake_phy_assigned_id = 1;
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let driver_features = Vec::new();
        let mut iface_response = create_iface_response(
            fake_role,
            fake_iface_id,
            fake_phy_id,
            fake_phy_assigned_id,
            fake_mac_addr,
            driver_features,
        );

        // Add the same iface ID twice
        for _ in 0..2 {
            // Add the fake iface
            let on_iface_added_fut = phy_manager.on_iface_added(fake_iface_id);
            pin_mut!(on_iface_added_fut);
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_query_iface_response(
                &mut exec,
                &mut test_values.dev_svc_stream,
                Some(&mut iface_response),
            );

            // Wait for the PhyManager to finish processing the received iface information
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_ready());
        }

        // Expect that the PhyContainer associated with the fake PHY has been updated with only one
        // reference to the fake client
        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert_eq!(phy_container.client_ifaces.len(), 1);
        assert!(phy_container.client_ifaces.contains_key(&fake_iface_id));
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnIfaceAdded event for
    /// an iface that has already been removed.  The PhyManager should fail to query the iface info
    /// and not account for the iface ID.
    #[fuchsia::test]
    fn add_nonexistent_iface() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        {
            // Add the non-existent iface
            let on_iface_added_fut = phy_manager.on_iface_added(1);
            pin_mut!(on_iface_added_fut);
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_query_iface_response(&mut exec, &mut test_values.dev_svc_stream, None);

            // Wait for the PhyManager to finish processing the received iface information
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_ready());
        }

        // Expect that the PhyContainer associated with the fake PHY has been updated with the
        // fake client
        assert!(phy_manager.phys.is_empty());
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnIfaceRemoved event
    /// for an iface that has been accounted for by the PhyManager.  The PhyManager should remove
    /// the iface ID from the PHY's list of client ifaces.
    #[run_singlethreaded(test)]
    async fn test_on_iface_removed() {
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();

        // Inject the fake PHY information
        let mut phy_container = PhyContainer::new(fake_mac_roles);
        let fake_iface_id = 1;
        let driver_features = Vec::new();
        let _ = phy_container.client_ifaces.insert(fake_iface_id, driver_features);

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        phy_manager.on_iface_removed(fake_iface_id);

        // Expect that the iface ID has been removed from the PhyContainer
        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.client_ifaces.is_empty());
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnIfaceRemoved event
    /// for an iface that has not been accounted for.  The PhyManager should simply ignore the
    /// request and leave its list of client iface IDs unchanged.
    #[run_singlethreaded(test)]
    async fn remove_missing_iface() {
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();

        let present_iface_id = 1;
        let removed_iface_id = 2;

        // Inject the fake PHY information
        let mut phy_container = PhyContainer::new(fake_mac_roles);
        let _ = phy_container.client_ifaces.insert(present_iface_id, Vec::new());
        let _ = phy_container.client_ifaces.insert(removed_iface_id, Vec::new());
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);
        phy_manager.on_iface_removed(removed_iface_id);

        // Expect that the iface ID has been removed from the PhyContainer
        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert_eq!(phy_container.client_ifaces.len(), 1);
        assert!(phy_container.client_ifaces.contains_key(&present_iface_id));
    }

    /// Tests the response of the PhyManager when a client iface is requested, but no PHYs are
    /// present.  The expectation is that the PhyManager returns None.
    #[run_singlethreaded(test)]
    async fn get_client_no_phys() {
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        let client = phy_manager.get_client();
        assert!(client.is_none());
    }

    /// Tests the response of the PhyManager when a client iface is requested, a client-capable PHY
    /// has been discovered, but client connections have not been started.  The expectation is that
    /// the PhyManager returns None.
    #[run_singlethreaded(test)]
    async fn get_unconfigured_client() {
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Client];
        let phy_container = PhyContainer::new(fake_mac_roles);

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Retrieve the client ID
        let client = phy_manager.get_client();
        assert!(client.is_none());
    }

    /// Tests the response of the PhyManager when a client iface is requested and a client iface is
    /// present.  The expectation is that the PhyManager should reply with the iface ID of the
    /// client iface.
    #[run_singlethreaded(test)]
    async fn get_configured_client() {
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Client];
        let phy_container = PhyContainer::new(fake_mac_roles);

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Insert the fake iface
        let fake_iface_id = 1;
        let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
        let driver_features = Vec::new();
        let _ = phy_container.client_ifaces.insert(fake_iface_id, driver_features);

        // Retrieve the client ID
        let client = phy_manager.get_client();
        assert_eq!(client.unwrap(), fake_iface_id)
    }

    /// Tests the response of the PhyManager when a client iface is requested and the only PHY
    /// that is present does not support client ifaces and has an AP iface present.  The
    /// expectation is that the PhyManager returns None.
    #[run_singlethreaded(test)]
    async fn get_client_no_compatible_phys() {
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Ap];
        let mut phy_container = PhyContainer::new(fake_mac_roles);
        let _ = phy_container.ap_ifaces.insert(fake_iface_id);
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Retrieve the client ID
        let client = phy_manager.get_client();
        assert!(client.is_none());
    }

    /// Tests the response of the PhyManager when a WPA3 capable client iface is requested and the
    /// only PHY that is present has a client iface that doesn't support WPA3 and has an AP iface
    /// present. The expectation is that the PhyManager returns None.
    #[run_singlethreaded(test)]
    async fn get_wpa3_client_no_compatible_client_phys() {
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Ap];
        let mut phy_container = PhyContainer::new(fake_mac_roles);
        let _ = phy_container.ap_ifaces.insert(1);
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);
        // Create a PhyContainer that has a client iface but no WPA3 support.
        let fake_phy_id_client = 2;
        let fake_mac_roles_client = vec![MacRole::Client];
        let mut phy_container_client = PhyContainer::new(fake_mac_roles_client);
        let driver_features = Vec::new();
        let _ = phy_container_client.client_ifaces.insert(2, driver_features);
        let _ = phy_manager.phys.insert(fake_phy_id_client, phy_container_client);

        // Retrieve the client ID
        let client = phy_manager.get_wpa3_capable_client();
        assert_eq!(client, None);
    }

    /// Tests the response of the PhyManager when a wpa3 capable client iface is requested and
    /// a matching client iface is present.  The expectation is that the PhyManager should reply
    /// with the iface ID of the client iface.
    #[test_case(fidl_common::DriverFeature::SaeDriverAuth)]
    #[test_case(fidl_common::DriverFeature::SaeSmeAuth)]
    #[fuchsia::test(add_test_attr = false)]
    fn get_configured_wpa3_client(wpa3_feature: fidl_common::DriverFeature) {
        let _exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer with WPA3 support to be inserted into the test
        // PhyManager
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Client];
        let mut phy_container = PhyContainer::new(fake_mac_roles);
        // Insert the fake iface
        let fake_iface_id = 1;
        let driver_features = vec![wpa3_feature];
        let _ = phy_container.client_ifaces.insert(fake_iface_id, driver_features);

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Retrieve the client ID
        let client = phy_manager.get_wpa3_capable_client();
        assert_eq!(client.unwrap(), fake_iface_id)
    }

    /// Tests the PhyManager's response to stop_client_connection when there is an existing client
    /// iface.  The expectation is that the client iface is destroyed and there is no remaining
    /// record of the iface ID in the PhyManager.
    #[fuchsia::test]
    fn destroy_all_client_ifaces() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Client];
        let phy_container = PhyContainer::new(fake_mac_roles);

        {
            let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            let driver_features = Vec::new();
            let _ = phy_container.client_ifaces.insert(fake_iface_id, driver_features);

            // Stop client connections
            let stop_clients_future = phy_manager.destroy_all_client_ifaces();
            pin_mut!(stop_clients_future);

            assert!(exec.run_until_stalled(&mut stop_clients_future).is_pending());

            send_destroy_iface_response(&mut exec, &mut test_values.monitor_stream, ZX_OK);

            assert!(exec.run_until_stalled(&mut stop_clients_future).is_ready());
        }

        // Ensure that the client interface that was added has been removed.
        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(!phy_container.client_ifaces.contains_key(&fake_iface_id));
    }

    /// Tests the PhyManager's response to destroy_all_client_ifaces when no client ifaces are
    /// present but an AP iface is present.  The expectation is that the AP iface is left intact.
    #[fuchsia::test]
    fn destroy_all_client_ifaces_no_clients() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Ap];
        let phy_container = PhyContainer::new(fake_mac_roles);

        // Insert the fake AP iface and then stop clients
        {
            let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake AP iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            let _ = phy_container.ap_ifaces.insert(fake_iface_id);

            // Stop client connections
            let stop_clients_future = phy_manager.destroy_all_client_ifaces();
            pin_mut!(stop_clients_future);

            assert!(exec.run_until_stalled(&mut stop_clients_future).is_ready());
        }

        // Ensure that the fake PHY and AP interface are still present.
        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.ap_ifaces.contains(&fake_iface_id));
    }

    /// Tests the PhyManager's response to a request for an AP when no PHYs are present.  The
    /// expectation is that the PhyManager will return None in this case.
    #[fuchsia::test]
    fn get_ap_no_phys() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        let get_ap_future = phy_manager.create_or_get_ap_iface();

        pin_mut!(get_ap_future);
        assert_variant!(exec.run_until_stalled(&mut get_ap_future), Poll::Ready(Ok(None)));
    }

    /// Tests the PhyManager's response when the PhyManager holds a PHY that can have an AP iface
    /// but the AP iface has not been created yet.  The expectation is that the PhyManager creates
    /// a new AP iface and returns its ID to the caller.
    #[fuchsia::test]
    fn get_unconfigured_ap() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Ap];
        let phy_container = PhyContainer::new(fake_mac_roles.clone());

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Retrieve the AP interface ID
        let fake_iface_id = 1;
        {
            let get_ap_future = phy_manager.create_or_get_ap_iface();

            pin_mut!(get_ap_future);
            assert!(exec.run_until_stalled(&mut get_ap_future).is_pending());

            send_create_iface_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Some(fake_iface_id),
            );
            assert_variant!(
                exec.run_until_stalled(&mut get_ap_future),
                Poll::Ready(Ok(Some(iface_id))) => assert_eq!(iface_id, fake_iface_id)
            );
        }

        assert!(phy_manager.phys[&fake_phy_id].ap_ifaces.contains(&fake_iface_id));
    }

    /// Tests the PhyManager's response to a create_or_get_ap_iface call when there is a PHY with an AP iface
    /// that has already been created.  The expectation is that the PhyManager should return the
    /// iface ID of the existing AP iface.
    #[fuchsia::test]
    fn get_configured_ap() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Ap];
        let phy_container = PhyContainer::new(fake_mac_roles);

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Insert the fake iface
        let fake_iface_id = 1;
        let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
        let _ = phy_container.ap_ifaces.insert(fake_iface_id);

        // Retrieve the AP iface ID
        let get_ap_future = phy_manager.create_or_get_ap_iface();
        pin_mut!(get_ap_future);
        assert_variant!(
            exec.run_until_stalled(&mut get_ap_future),
            Poll::Ready(Ok(Some(iface_id))) => assert_eq!(iface_id, fake_iface_id)
        );
    }

    /// This test attempts to get an AP iface from a PhyManager that has a PHY that can only have
    /// a client interface.  The PhyManager should return None.
    #[fuchsia::test]
    fn get_ap_no_compatible_phys() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Client];
        let phy_container = PhyContainer::new(fake_mac_roles);

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Retrieve the client ID
        let get_ap_future = phy_manager.create_or_get_ap_iface();
        pin_mut!(get_ap_future);
        assert_variant!(exec.run_until_stalled(&mut get_ap_future), Poll::Ready(Ok(None)));
    }

    /// This test stops a valid AP iface on a PhyManager.  The expectation is that the PhyManager
    /// should retain the record of the PHY, but the AP iface ID should be removed.
    #[fuchsia::test]
    fn stop_valid_ap_iface() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Ap];

        {
            let phy_container = PhyContainer::new(fake_mac_roles.clone());

            let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            let _ = phy_container.ap_ifaces.insert(fake_iface_id);

            // Remove the AP iface ID
            let destroy_ap_iface_future = phy_manager.destroy_ap_iface(fake_iface_id);
            pin_mut!(destroy_ap_iface_future);
            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_pending());
            send_destroy_iface_response(&mut exec, &mut test_values.monitor_stream, ZX_OK);

            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(!phy_container.ap_ifaces.contains(&fake_iface_id));
    }

    /// This test attempts to stop an invalid AP iface ID.  The expectation is that a valid iface
    /// ID is unaffected.
    #[fuchsia::test]
    fn stop_invalid_ap_iface() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Ap];

        {
            let phy_container = PhyContainer::new(fake_mac_roles);

            let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            let _ = phy_container.ap_ifaces.insert(fake_iface_id);

            // Remove a non-existent AP iface ID
            let destroy_ap_iface_future = phy_manager.destroy_ap_iface(2);
            pin_mut!(destroy_ap_iface_future);
            assert_variant!(
                exec.run_until_stalled(&mut destroy_ap_iface_future),
                Poll::Ready(Ok(()))
            );
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.ap_ifaces.contains(&fake_iface_id));
    }

    /// This test creates two AP ifaces for a PHY that supports AP ifaces.  destroy_all_ap_ifaces is then
    /// called on the PhyManager.  The expectation is that both AP ifaces should be destroyed and
    /// the records of the iface IDs should be removed from the PhyContainer.
    #[fuchsia::test]
    fn stop_all_ap_ifaces() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // ifaces are added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Ap];

        {
            let phy_container = PhyContainer::new(fake_mac_roles.clone());

            let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            let _ = phy_container.ap_ifaces.insert(0);
            let _ = phy_container.ap_ifaces.insert(1);

            // Expect two interface destruction requests
            let destroy_ap_iface_future = phy_manager.destroy_all_ap_ifaces();
            pin_mut!(destroy_ap_iface_future);

            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_pending());
            send_destroy_iface_response(&mut exec, &mut test_values.monitor_stream, ZX_OK);

            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_pending());
            send_destroy_iface_response(&mut exec, &mut test_values.monitor_stream, ZX_OK);

            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.ap_ifaces.is_empty());
    }

    /// This test calls destroy_all_ap_ifaces on a PhyManager that only has a client iface.  The expectation
    /// is that no interfaces should be destroyed and the client iface ID should remain in the
    /// PhyManager
    #[fuchsia::test]
    fn stop_all_ap_ifaces_with_client() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Client];

        {
            let phy_container = PhyContainer::new(fake_mac_roles);
            let driver_features = Vec::new();

            let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            let _ = phy_container.client_ifaces.insert(fake_iface_id, driver_features);

            // Stop all AP ifaces
            let destroy_ap_iface_future = phy_manager.destroy_all_ap_ifaces();
            pin_mut!(destroy_ap_iface_future);
            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.client_ifaces.contains_key(&fake_iface_id));
    }

    /// Verifies that setting a suggested AP MAC address results in that MAC address being used as
    /// a part of the request to create an AP interface.  Ensures that this does not affect client
    /// interface requests.
    #[fuchsia::test]
    fn test_suggest_ap_mac() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Ap];
        let driver_features = Vec::new();
        let phy_container = PhyContainer::new(fake_mac_roles.clone());

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Insert the fake iface
        let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
        let _ = phy_container.client_ifaces.insert(fake_iface_id, driver_features);

        // Suggest an AP MAC
        let mac = MacAddress::from_bytes(&[1, 2, 3, 4, 5, 6]).unwrap();
        phy_manager.suggest_ap_mac(mac.clone());

        let get_ap_future = phy_manager.create_or_get_ap_iface();
        pin_mut!(get_ap_future);
        assert_variant!(exec.run_until_stalled(&mut get_ap_future), Poll::Pending);

        // Verify that the suggested MAC is included in the request
        assert_variant!(
            exec.run_until_stalled(&mut test_values.monitor_stream.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceMonitorRequest::CreateIface {
                    req,
                    responder,
                }
            ))) => {
                let requested_mac = match req.mac_addr {
                    Some(mac) => MacAddress::from_bytes(&mac).unwrap(),
                    None => panic!("requested mac is None")
                };
                assert_eq!(requested_mac, mac);
                let mut response = fidl_service::CreateIfaceResponse { iface_id: fake_iface_id };
                let response = Some(&mut response);
                responder.send(ZX_OK, response).expect("sending fake iface id");
            }
        );
        assert_variant!(exec.run_until_stalled(&mut get_ap_future), Poll::Ready(_));
    }

    #[fuchsia::test]
    fn test_suggested_mac_does_not_apply_to_client() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_role = fidl_fuchsia_wlan_device::MacRole::Client;
        let fake_mac_roles = vec![fake_mac_role.clone()];
        let phy_container = PhyContainer::new(fake_mac_roles.clone());

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Suggest an AP MAC
        let mac = MacAddress::from_bytes(&[1, 2, 3, 4, 5, 6]);
        phy_manager.suggest_ap_mac(mac.clone().unwrap());

        // Start client connections so that an IfaceRequest is issued for the client.
        let start_client_future =
            phy_manager.create_all_client_ifaces(CreateClientIfacesReason::StartClientConnections);
        pin_mut!(start_client_future);
        assert_variant!(exec.run_until_stalled(&mut start_client_future), Poll::Pending);

        // Verify that the suggested MAC is NOT included in the request
        assert_variant!(
            exec.run_until_stalled(&mut test_values.monitor_stream.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceMonitorRequest::CreateIface {
                    req,
                    responder,
                }
            ))) => {
                assert!(req.mac_addr.is_none());
                let mut response = fidl_service::CreateIfaceResponse { iface_id: fake_iface_id };
                let response = Some(&mut response);
                responder.send(ZX_OK, response).expect("sending fake iface id");
            }
        );
        // Expect an IfaceQuery to get driver features. and send back a response
        assert_variant!(exec.run_until_stalled(&mut start_client_future), Poll::Pending);
        let fake_phy_assigned_id = 1;
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let driver_features = Vec::new();
        let mut iface_response = create_iface_response(
            fake_mac_role,
            fake_iface_id,
            fake_phy_id,
            fake_phy_assigned_id,
            fake_mac_addr,
            driver_features,
        );
        send_query_iface_response(
            &mut exec,
            &mut test_values.dev_svc_stream,
            Some(&mut iface_response),
        );
        assert_variant!(exec.run_until_stalled(&mut start_client_future), Poll::Ready(_));
    }

    /// Tests get_phy_ids() when no PHYs are present. The expectation is that the PhyManager will
    /// return an empty `Vec` in this case.
    #[run_singlethreaded(test)]
    async fn get_phy_ids_no_phys() {
        let test_values = test_setup();
        let phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);
        assert_eq!(phy_manager.get_phy_ids(), Vec::<u16>::new());
    }

    /// Tests get_phy_ids() when a single PHY is present. The expectation is that the PhyManager will
    /// return a single element `Vec`, with the appropriate ID.
    #[fuchsia::test]
    fn get_phy_ids_single_phy() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        {
            let phy_info = fake_phy_info(1, vec![]);
            let add_phy_fut = phy_manager.add_phy(phy_info.id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());
            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Some(vec![]),
            );
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        assert_eq!(phy_manager.get_phy_ids(), vec![1]);
    }

    /// Tests get_phy_ids() when two PHYs are present. The expectation is that the PhyManager will
    /// return a two-element `Vec`, containing the appropriate IDs. Ordering is not guaranteed.
    #[fuchsia::test]
    fn get_phy_ids_two_phys() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        {
            let phy_info = fake_phy_info(1, Vec::new());
            let add_phy_fut = phy_manager.add_phy(phy_info.id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());
            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Some(vec![]),
            );
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        {
            let phy_info = fake_phy_info(2, Vec::new());
            let add_phy_fut = phy_manager.add_phy(phy_info.id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());
            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Some(vec![]),
            );
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        let phy_ids = phy_manager.get_phy_ids();
        assert!(phy_ids.contains(&1), "expected phy_ids to contain `1`, but phy_ids={:?}", phy_ids);
        assert!(phy_ids.contains(&2), "expected phy_ids to contain `2`, but phy_ids={:?}", phy_ids);
    }

    /// Tests log_phy_add_failure() to ensure the appropriate inspect count is incremented by 1.
    #[run_singlethreaded(test)]
    async fn log_phy_add_failure() {
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        assert_data_tree!(test_values.inspector, root: {
            phy_manager: {
                phy_add_fail_count: 0u64,
            },
        });

        phy_manager.log_phy_add_failure();
        assert_data_tree!(test_values.inspector, root: {
            phy_manager: {
                phy_add_fail_count: 1u64,
            },
        });
    }

    /// Tests the initialization of the country code and the ability of the PhyManager to cache a
    /// country code update.
    #[fuchsia::test]
    fn test_set_country_code() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Insert a couple fake PHYs.
        let _ = phy_manager.phys.insert(
            0,
            PhyContainer {
                supported_mac_roles: vec![],
                client_ifaces: HashMap::new(),
                ap_ifaces: HashSet::new(),
            },
        );
        let _ = phy_manager.phys.insert(
            1,
            PhyContainer {
                supported_mac_roles: vec![],
                client_ifaces: HashMap::new(),
                ap_ifaces: HashSet::new(),
            },
        );

        // Initially the country code should be unset.
        assert!(phy_manager.saved_country_code.is_none());

        // Apply a country code and ensure that it is propagated to the device service.
        {
            let set_country_fut = phy_manager.set_country_code(Some([0, 1]));
            pin_mut!(set_country_fut);

            // Ensure that both PHYs have their country codes set.
            for _ in 0..2 {
                assert_variant!(exec.run_until_stalled(&mut set_country_fut), Poll::Pending);
                assert_variant!(
                    exec.run_until_stalled(&mut test_values.monitor_stream.next()),
                    Poll::Ready(Some(Ok(
                        fidl_service::DeviceMonitorRequest::SetCountry {
                            req: fidl_service::SetCountryRequest {
                                phy_id: _,
                                alpha2: [0, 1],
                            },
                            responder,
                        }
                    ))) => {
                        responder.send(ZX_OK).expect("sending fake set country response");
                    }
                );
            }

            assert_variant!(exec.run_until_stalled(&mut set_country_fut), Poll::Ready(Ok(())));
        }
        assert_eq!(phy_manager.saved_country_code, Some([0, 1]));

        // Unset the country code and ensure that the clear country code message is sent to the
        // device service.
        {
            let set_country_fut = phy_manager.set_country_code(None);
            pin_mut!(set_country_fut);

            // Ensure that both PHYs have their country codes cleared.
            for _ in 0..2 {
                assert_variant!(exec.run_until_stalled(&mut set_country_fut), Poll::Pending);
                assert_variant!(
                    exec.run_until_stalled(&mut test_values.monitor_stream.next()),
                    Poll::Ready(Some(Ok(
                        fidl_service::DeviceMonitorRequest::ClearCountry {
                            req: fidl_service::ClearCountryRequest {
                                phy_id: _,
                            },
                            responder,
                        }
                    ))) => {
                        responder.send(ZX_OK).expect("sending fake clear country response");
                    }
                );
            }

            assert_variant!(exec.run_until_stalled(&mut set_country_fut), Poll::Ready(Ok(())));
        }
        assert_eq!(phy_manager.saved_country_code, None);
    }

    // Tests the case where setting the country code is unsuccessful.
    #[fuchsia::test]
    fn test_setting_country_code_fails() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Insert a fake PHY.
        let _ = phy_manager.phys.insert(
            0,
            PhyContainer {
                supported_mac_roles: vec![],
                client_ifaces: HashMap::new(),
                ap_ifaces: HashSet::new(),
            },
        );

        // Initially the country code should be unset.
        assert!(phy_manager.saved_country_code.is_none());

        // Apply a country code and ensure that it is propagated to the device service.
        {
            let set_country_fut = phy_manager.set_country_code(Some([0, 1]));
            pin_mut!(set_country_fut);

            assert_variant!(exec.run_until_stalled(&mut set_country_fut), Poll::Pending);
            assert_variant!(
                exec.run_until_stalled(&mut test_values.monitor_stream.next()),
                Poll::Ready(Some(Ok(
                    fidl_service::DeviceMonitorRequest::SetCountry {
                        req: fidl_service::SetCountryRequest {
                            phy_id: 0,
                            alpha2: [0, 1],
                        },
                        responder,
                    }
                ))) => {
                    // Send back a failure.
                    responder
                        .send(fuchsia_zircon::sys::ZX_ERR_NOT_SUPPORTED)
                        .expect("sending fake set country response");
                }
            );

            assert_variant!(
                exec.run_until_stalled(&mut set_country_fut),
                Poll::Ready(Err(PhyManagerError::PhySetCountryFailure))
            );
        }
        assert_eq!(phy_manager.saved_country_code, Some([0, 1]));
    }

    /// Tests the case where multiple client interfaces need to be recovered.
    #[fuchsia::test]
    fn test_recover_client_interfaces_succeeds() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Client];

        // Make it look like client connections have been enabled.
        phy_manager.client_connections_enabled = true;

        // Create four fake PHY entries.  For the sake of this test, each PHY will eventually
        // receive and interface ID equal to its PHY ID.
        for phy_id in 0..4 {
            let fake_mac_roles = fake_mac_roles.clone();
            let _ = phy_manager.phys.insert(phy_id, PhyContainer::new(fake_mac_roles.clone()));

            // Give the 0th and 2nd PHYs have client interfaces.
            if phy_id % 2 == 0 {
                let phy_container = phy_manager.phys.get_mut(&phy_id).expect("missing PHY");
                let _ = phy_container.client_ifaces.insert(phy_id, Vec::new());
            }
        }

        // There are now two PHYs with client interfaces and two without.  This looks like two
        // interfaces have undergone recovery.  Run recover_client_ifaces and ensure that the two
        // PHYs that are missing client interfaces have interfaces created for them.
        {
            let recovery_fut =
                phy_manager.create_all_client_ifaces(CreateClientIfacesReason::RecoverClientIfaces);
            pin_mut!(recovery_fut);
            assert_variant!(exec.run_until_stalled(&mut recovery_fut), Poll::Pending);

            loop {
                // The recovery future will only stall out when either
                // 1. It needs to create a client interface for a PHY that does not have one.
                // 2. The futures completes and has recovered all possible interfaces.
                match exec.run_until_stalled(&mut recovery_fut) {
                    Poll::Pending => {}
                    Poll::Ready(result) => {
                        let iface_ids = result.expect("recovery failed unexpectedly");
                        assert!(iface_ids.contains(&1));
                        assert!(iface_ids.contains(&3));
                        break;
                    }
                }

                // Make sure that the stalled future has made a FIDL request to create a client
                // interface.  Send back a response assigning an interface ID equal to the PHY ID.
                assert_variant!(
                exec.run_until_stalled(&mut test_values.monitor_stream.next()),
                Poll::Ready(Some(Ok(
                    fidl_service::DeviceMonitorRequest::CreateIface {
                        req,
                        responder,
                    }
                ))) => {
                    let mut response =
                        fidl_service::CreateIfaceResponse { iface_id: req.phy_id };
                    let response = Some(&mut response);
                    responder.send(ZX_OK, response).expect("sending fake iface id");

                    // Expect and respond to a QueryIface request.
                    let mut iface_response = create_iface_response(
                        fake_mac_roles[0],
                        req.phy_id,
                        req.phy_id,
                        0,
                        [0; 6],
                        Vec::new(),
                    );
                    assert_variant!(exec.run_until_stalled(&mut recovery_fut), Poll::Pending);
                    send_query_iface_response(
                        &mut exec,
                        &mut test_values.dev_svc_stream,
                        Some(&mut iface_response));
                });
            }
        }

        // Make sure all of the PHYs have interface IDs and that the IDs match the PHY IDs,
        // indicating that they were assigned correctly.
        for phy_id in phy_manager.phys.keys() {
            assert_eq!(phy_manager.phys[phy_id].client_ifaces.len(), 1);
            assert!(phy_manager.phys[phy_id].client_ifaces.contains_key(phy_id));
        }
    }

    /// Tests the case where a client interface needs to be recovered and recovery fails.
    #[fuchsia::test]
    fn test_recover_client_interfaces_fails() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Client];

        // Make it look like client connections have been enabled.
        phy_manager.client_connections_enabled = true;

        // For this test, use three PHYs (0, 1, and 2).  Let recovery fail for PHYs 0 and 2 and
        // succeed for PHY 1.  Verify that a create interface request is sent for each PHY and at
        // the end, verify that only one recovered interface is listed and that PHY 1 has been
        // assigned that interface.
        for phy_id in 0..3 {
            let _ = phy_manager.phys.insert(phy_id, PhyContainer::new(fake_mac_roles.clone()));
        }

        // Run recovery.
        {
            let recovery_fut =
                phy_manager.create_all_client_ifaces(CreateClientIfacesReason::RecoverClientIfaces);
            pin_mut!(recovery_fut);
            assert_variant!(exec.run_until_stalled(&mut recovery_fut), Poll::Pending);

            loop {
                match exec.run_until_stalled(&mut recovery_fut) {
                    Poll::Pending => {}
                    Poll::Ready(result) => {
                        let iface_ids = assert_variant!(result, Err((iface_ids, _)) => iface_ids);
                        assert_eq!(iface_ids, vec![1]);
                        break;
                    }
                }

                // Keep track of the iface ID if created to expect an iface query if required.
                let mut created_iface_id = None;

                // Make sure that the stalled future has made a FIDL request to create a client
                // interface.  Send back a response assigning an interface ID equal to the PHY ID.
                assert_variant!(
                    exec.run_until_stalled(&mut test_values.monitor_stream.next()),
                    Poll::Ready(Some(Ok(
                        fidl_service::DeviceMonitorRequest::CreateIface {
                            req,
                            responder,
                        }
                    ))) => {
                        let iface_id = req.phy_id;
                        let mut response =
                            fidl_service::CreateIfaceResponse { iface_id };
                        let response = Some(&mut response);

                        // As noted above, let the requests for 0 and 2 "fail" and let the request
                        // for PHY 1 succeed.
                        let result_code = match req.phy_id {
                            1 => {
                                created_iface_id = Some(iface_id);
                                ZX_OK
                            },
                            _ => ZX_ERR_NOT_FOUND,
                        };

                        responder.send(result_code, response).expect("sending fake iface id");
                    }
                );

                // If creating the iface succeeded, respond to the QueryIface request
                if let Some(iface_id) = created_iface_id {
                    let mut iface_response = create_iface_response(
                        fake_mac_roles[0],
                        iface_id,
                        iface_id,
                        0,
                        [0; 6],
                        Vec::new(),
                    );
                    assert_variant!(exec.run_until_stalled(&mut recovery_fut), Poll::Pending);
                    send_query_iface_response(
                        &mut exec,
                        &mut test_values.dev_svc_stream,
                        Some(&mut iface_response),
                    );
                }
            }
        }

        // Make sure PHYs 0 and 2 do not have interfaces and that PHY 1 does.
        for phy_id in phy_manager.phys.keys() {
            match phy_id {
                1 => {
                    assert_eq!(phy_manager.phys[phy_id].client_ifaces.len(), 1);
                    assert!(phy_manager.phys[phy_id].client_ifaces.contains_key(phy_id));
                }
                _ => assert!(phy_manager.phys[phy_id].client_ifaces.is_empty()),
            }
        }
    }

    /// Tests the case where a PHY is client-capable, but client connections are disabled and a
    /// caller requests attempts to recover client interfaces.
    #[fuchsia::test]
    fn test_recover_client_interfaces_while_disabled() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        // Create a fake PHY entry without client interfaces.  Note that client connections have
        // not been set to enabled.
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Client];
        let _ = phy_manager.phys.insert(0, PhyContainer::new(fake_mac_roles));

        // Run recovery and ensure that it completes immediately and does not recover any
        // interfaces.
        {
            let recovery_fut =
                phy_manager.create_all_client_ifaces(CreateClientIfacesReason::RecoverClientIfaces);
            pin_mut!(recovery_fut);
            assert_variant!(
                exec.run_until_stalled(&mut recovery_fut),
                Poll::Ready(Ok(recovered_ifaces)) => {
                    assert!(recovered_ifaces.is_empty());
                }
            );
        }

        // Verify that there are no client interfaces.
        for (_, phy_container) in phy_manager.phys {
            assert!(phy_container.client_ifaces.is_empty());
        }
    }

    #[test_case(fidl_common::DriverFeature::SaeDriverAuth)]
    #[test_case(fidl_common::DriverFeature::SaeSmeAuth)]
    #[fuchsia::test(add_test_attr = false)]
    fn has_wpa3_client_iface(wpa3_feature: fidl_common::DriverFeature) {
        let _exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Create a phy with the feature that indicates WPA3 support.
        let driver_features = vec![fidl_common::DriverFeature::ScanOffload, wpa3_feature];
        let fake_phy_id = 0;

        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);
        let mut phy_container = PhyContainer::new(vec![]);
        let _ = phy_container.client_ifaces.insert(0, driver_features);
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Check that has_wpa3_client_iface recognizes that WPA3 is supported
        assert_eq!(phy_manager.has_wpa3_client_iface(), true);

        // Add another phy that does not support WPA3.
        let fake_phy_id = 1;
        let phy_container = PhyContainer::new(vec![]);
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Phy manager has at least one WPA3 capable iface, so has_wpa3_iface should still be true.
        assert_eq!(phy_manager.has_wpa3_client_iface(), true);
    }

    #[fuchsia::test]
    fn has_no_wpa3_capable_client_iface() {
        let _exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        // Create a phy with driver features, but not the one that indicates WPA3 support.
        let driver_features =
            vec![fidl_common::DriverFeature::ScanOffload, fidl_common::DriverFeature::Dfs];
        let fake_phy_id = 0;
        let mut phy_container = PhyContainer::new(vec![]);
        let _ = phy_container.client_ifaces.insert(0, driver_features);

        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        assert_eq!(phy_manager.has_wpa3_client_iface(), false);
    }

    /// Tests reporting of client connections status when client connections are enabled.
    #[fuchsia::test]
    fn test_client_connections_enabled_when_enabled() {
        let _exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        phy_manager.client_connections_enabled = true;
        assert!(phy_manager.client_connections_enabled());
    }

    /// Tests reporting of client connections status when client connections are disabled.
    #[fuchsia::test]
    fn test_client_connections_enabled_when_disabled() {
        let _exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager =
            PhyManager::new(test_values.dev_svc_proxy, test_values.monitor_proxy, test_values.node);

        phy_manager.client_connections_enabled = false;
        assert!(!phy_manager.client_connections_enabled());
    }
}
