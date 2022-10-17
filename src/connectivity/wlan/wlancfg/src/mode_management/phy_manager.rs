// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        mode_management::{
            iface_manager::wpa3_supported, Defect, EventHistory, IfaceFailure, PhyFailure,
        },
        regulatory_manager::REGION_CODE_LEN,
        telemetry::{TelemetryEvent, TelemetrySender},
    },
    async_trait::async_trait,
    eui48::MacAddress,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_device_service as fidl_service,
    fuchsia_inspect::{self as inspect, NumericProperty},
    fuchsia_zircon,
    ieee80211::{MacAddr, NULL_MAC_ADDR},
    log::{error, info, warn},
    std::collections::{HashMap, HashSet},
    thiserror::Error,
};

// Number of seconds that recoverable event histories should be stored.  Since thresholds have not
// yet been established, zero this out so that memory does not increase unnecessarily.
const DEFECT_RETENTION_SECONDS: u32 = 0;

/// Errors raised while attempting to query information about or configure PHYs and ifaces.
#[derive(Debug, Error)]
pub enum PhyManagerError {
    #[error("the requested operation is not supported")]
    Unsupported,
    #[error("unable to query phy information")]
    PhyQueryFailure,
    #[error("failed to set country for new PHY")]
    PhySetCountryFailure,
    #[error("unable to apply power setting")]
    PhySetLowPowerFailure,
    #[error("unable to query iface information")]
    IfaceQueryFailure,
    #[error("unable to create iface")]
    IfaceCreateFailure,
    #[error("unable to destroy iface")]
    IfaceDestroyFailure,
    #[error("internal state has become inconsistent")]
    InternalError,
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
    supported_mac_roles: HashSet<fidl_common::WlanMacRole>,
    // Security support is tracked for each interface so that callers can request an interface
    // based on security features (e.g. for WPA3).
    client_ifaces: HashMap<u16, fidl_common::SecuritySupport>,
    ap_ifaces: HashSet<u16>,
    defects: EventHistory<Defect>,
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

    /// Sets the low power state for all PHYs.  PHYs discovered in the future will have the low
    /// power setting applied to them as well.
    async fn set_power_state(
        &mut self,
        power_state: fidl_common::PowerSaveType,
    ) -> Result<fuchsia_zircon::Status, anyhow::Error>;

    /// Store a record for the provided defect.  When it becomes possible, potentially recover the
    /// offending PHY.
    async fn record_defect(&mut self, defect: Defect);
}

/// Maintains a record of all PHYs that are present and their associated interfaces.
pub struct PhyManager {
    phys: HashMap<u16, PhyContainer>,
    device_monitor: fidl_service::DeviceMonitorProxy,
    client_connections_enabled: bool,
    power_state: fidl_common::PowerSaveType,
    suggested_ap_mac: Option<MacAddress>,
    saved_country_code: Option<[u8; REGION_CODE_LEN]>,
    _node: inspect::Node,
    telemetry_sender: TelemetrySender,
    phy_add_fail_count: inspect::UintProperty,
}

impl PhyContainer {
    /// Stores the PhyInfo associated with a newly discovered PHY and creates empty vectors to hold
    /// interface IDs that belong to this PHY.
    pub fn new(supported_mac_roles: Vec<fidl_common::WlanMacRole>) -> Self {
        PhyContainer {
            supported_mac_roles: supported_mac_roles.into_iter().collect(),
            client_ifaces: HashMap::new(),
            ap_ifaces: HashSet::new(),
            defects: EventHistory::<Defect>::new(DEFECT_RETENTION_SECONDS),
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
        device_monitor: fidl_service::DeviceMonitorProxy,
        node: inspect::Node,
        telemetry_sender: TelemetrySender,
    ) -> Self {
        let phy_add_fail_count = node.create_uint("phy_add_fail_count", 0);
        PhyManager {
            phys: HashMap::new(),
            device_monitor,
            client_connections_enabled: false,
            power_state: fidl_common::PowerSaveType::PsModePerformance,
            suggested_ap_mac: None,
            saved_country_code: None,
            _node: node,
            telemetry_sender,
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
        Ok(self.phys.get_mut(&phy_id).ok_or_else(|| {
            error!("Phy ID did not exist in self.phys");
            PhyManagerError::InternalError
        }))?
    }

    /// Queries the information associated with the given iface ID.
    async fn query_iface(
        &self,
        iface_id: u16,
    ) -> Result<Option<fidl_service::QueryIfaceResponse>, PhyManagerError> {
        match self.device_monitor.query_iface(iface_id).await {
            Ok(Ok(response)) => Ok(Some(response)),
            Ok(Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND)) => Ok(None),
            _ => Err(PhyManagerError::IfaceQueryFailure),
        }
    }

    /// Returns a list of PHY IDs that can have interfaces of the requested MAC role.
    fn phys_for_role(&self, role: fidl_common::WlanMacRole) -> Vec<u16> {
        self.phys
            .iter()
            .filter_map(|(k, v)| {
                if v.supported_mac_roles.contains(&role) {
                    return Some(*k);
                }
                None
            })
            .collect()
    }
}

#[async_trait]
impl PhyManagerApi for PhyManager {
    async fn add_phy(&mut self, phy_id: u16) -> Result<(), PhyManagerError> {
        let supported_mac_roles = self
            .device_monitor
            .get_supported_mac_roles(phy_id)
            .await
            .map_err(|e| {
                warn!("Failed to communicate with monitor service: {:?}", e);
                PhyManagerError::PhyQueryFailure
            })?
            .map_err(|e| {
                warn!("Unable to get supported MAC roles: {:?}", e);
                PhyManagerError::PhyQueryFailure
            })?;

        // Create a new container to store the PHY's information.
        info!("adding PHY ID #{}", phy_id);
        let mut phy_container = PhyContainer::new(supported_mac_roles);

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
            && phy_container.supported_mac_roles.contains(&fidl_common::WlanMacRole::Client)
        {
            let iface_id = create_iface(
                &self.device_monitor,
                phy_id,
                fidl_common::WlanMacRole::Client,
                NULL_MAC_ADDR,
                &self.telemetry_sender,
            )
            .await?;
            let security_support = query_security_support(&self.device_monitor, iface_id).await?;
            if phy_container.client_ifaces.insert(iface_id, security_support).is_some() {
                warn!("Unexpectedly replaced existing iface security support for id {}", iface_id);
            };
        }

        if self.power_state != fidl_common::PowerSaveType::PsModePerformance {
            let ps_result = set_ps_mode(&self.device_monitor, phy_id, self.power_state)
                .await
                .map_err(|_| PhyManagerError::PhySetLowPowerFailure)?;
            fuchsia_zircon::ok(ps_result).map_err(|_| PhyManagerError::PhySetLowPowerFailure)?
        }

        if self.phys.insert(phy_id, phy_container).is_some() {
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
            let iface_id = query_iface_response.id;
            let security_support = query_security_support(&self.device_monitor, iface_id).await?;
            let phy = self.ensure_phy(query_iface_response.phy_id).await?;

            match query_iface_response.role {
                fidl_common::WlanMacRole::Client => {
                    if let Some(old_security_support) =
                        phy.client_ifaces.insert(iface_id, security_support)
                    {
                        if old_security_support != security_support {
                            warn!("Security support changed unexpectedly for id {}", iface_id);
                        }
                    } else {
                        // The iface wasn't in the hashmap, so it was created by someone else
                        warn!("Detected an unexpected client iface id {} created outside of PhyManager", iface_id);
                    }
                }
                fidl_common::WlanMacRole::Ap => {
                    if phy.ap_ifaces.insert(iface_id) {
                        // `.insert()` returns true if the value was not already present
                        warn!("Detected an unexpected AP iface created outside of PhyManager");
                    }
                }
                fidl_common::WlanMacRole::Mesh => {
                    return Err(PhyManagerError::Unsupported);
                }
            }
        }
        Ok(())
    }

    fn on_iface_removed(&mut self, iface_id: u16) {
        for (_, phy_info) in self.phys.iter_mut() {
            // The presence or absence of the interface in the PhyManager internal records is
            // irrelevant.  Simply remove any reference to the removed interface ID to ensure that
            // it is not used for future operations.
            let _ = phy_info.client_ifaces.remove(&iface_id);
            let _ = phy_info.ap_ifaces.remove(&iface_id);
        }
    }

    async fn create_all_client_ifaces(
        &mut self,
        reason: CreateClientIfacesReason,
    ) -> Result<Vec<u16>, (Vec<u16>, PhyManagerError)> {
        if reason == CreateClientIfacesReason::StartClientConnections {
            self.client_connections_enabled = true;
        }

        let mut available_iface_ids = Vec::new();
        let mut error_encountered = Ok(());

        if self.client_connections_enabled {
            let client_capable_phy_ids = self.phys_for_role(fidl_common::WlanMacRole::Client);

            for client_phy in client_capable_phy_ids.iter() {
                let phy_container = match self.phys.get_mut(client_phy) {
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
                        fidl_common::WlanMacRole::Client,
                        NULL_MAC_ADDR,
                        &self.telemetry_sender,
                    )
                    .await
                    {
                        Ok(iface_id) => iface_id,
                        Err(e) => {
                            warn!("Failed to recover iface for PHY {}: {:?}", client_phy, e);
                            error_encountered = Err(e);
                            self.record_defect(Defect::Phy(PhyFailure::IfaceCreationFailure {
                                phy_id: *client_phy,
                            }))
                            .await;
                            continue;
                        }
                    };
                    let security_support = query_security_support(&self.device_monitor, iface_id)
                        .await
                        .unwrap_or_else(|e| {
                            error!("Error occurred getting iface security support: {}", e);
                            fidl_common::SecuritySupport {
                                sae: fidl_common::SaeFeature {
                                    driver_handler_supported: false,
                                    sme_handler_supported: false,
                                },
                                mfp: fidl_common::MfpFeature { supported: false },
                            }
                        });
                    let _ = phy_container.client_ifaces.insert(iface_id, security_support);
                }

                // Regardless of whether or not new interfaces were created or existing interfaces
                // were discovered, notify the caller of all interface IDs available for this PHY.
                let mut available_interfaces =
                    phy_container.client_ifaces.keys().into_iter().cloned().collect();
                available_iface_ids.append(&mut available_interfaces);
            }
        }

        match error_encountered {
            Ok(()) => Ok(available_iface_ids),
            Err(e) => Err((available_iface_ids, e)),
        }
    }

    fn client_connections_enabled(&self) -> bool {
        self.client_connections_enabled
    }

    async fn destroy_all_client_ifaces(&mut self) -> Result<(), PhyManagerError> {
        self.client_connections_enabled = false;

        let client_capable_phys = self.phys_for_role(fidl_common::WlanMacRole::Client);
        let mut result = Ok(());
        let mut failing_phys = Vec::new();

        for client_phy in client_capable_phys.iter() {
            let mut phy_container =
                self.phys.get_mut(client_phy).ok_or(PhyManagerError::PhyQueryFailure)?;

            // Continue tracking interface IDs for which deletion fails.
            let mut lingering_ifaces = HashMap::new();

            for (iface_id, security_support) in phy_container.client_ifaces.drain() {
                match destroy_iface(&self.device_monitor, iface_id, &self.telemetry_sender).await {
                    Ok(()) => {}
                    Err(e) => {
                        result = Err(e);
                        failing_phys.push(*client_phy);
                        if lingering_ifaces.insert(iface_id, security_support).is_some() {
                            warn!("Unexpected duplicate lingering iface for id {}", iface_id);
                        };
                    }
                }
            }
            phy_container.client_ifaces = lingering_ifaces;
        }

        if result.is_err() {
            for phy_id in failing_phys {
                self.record_defect(Defect::Phy(PhyFailure::IfaceDestructionFailure { phy_id }))
                    .await
            }
        }

        result
    }

    fn get_client(&mut self) -> Option<u16> {
        if !self.client_connections_enabled {
            return None;
        }

        let client_capable_phys = self.phys_for_role(fidl_common::WlanMacRole::Client);
        if client_capable_phys.is_empty() {
            return None;
        }

        // Find the first PHY with any client interfaces and return its first client interface.
        let phy = self.phys.get_mut(&client_capable_phys[0])?;
        phy.client_ifaces.keys().next().map(|iface_id| *iface_id)
    }

    fn get_wpa3_capable_client(&mut self) -> Option<u16> {
        // Search phys for a client iface with security support indicating WPA3 support.
        for (_, phy) in self.phys.iter() {
            for (iface_id, security_support) in phy.client_ifaces.iter() {
                if wpa3_supported(*security_support) {
                    return Some(*iface_id);
                }
            }
        }
        None
    }

    async fn create_or_get_ap_iface(&mut self) -> Result<Option<u16>, PhyManagerError> {
        let ap_capable_phy_ids = self.phys_for_role(fidl_common::WlanMacRole::Ap);
        if ap_capable_phy_ids.is_empty() {
            return Ok(None);
        }

        // First check for any PHYs that can have AP interfaces but do not yet
        for ap_phy_id in ap_capable_phy_ids.iter() {
            let phy_container =
                self.phys.get_mut(ap_phy_id).ok_or(PhyManagerError::PhyQueryFailure)?;
            if phy_container.ap_ifaces.is_empty() {
                let mac = match self.suggested_ap_mac {
                    Some(mac) => mac.to_array(),
                    None => NULL_MAC_ADDR,
                };
                let iface_id = match create_iface(
                    &self.device_monitor,
                    *ap_phy_id,
                    fidl_common::WlanMacRole::Ap,
                    mac,
                    &self.telemetry_sender,
                )
                .await
                {
                    Ok(iface_id) => iface_id,
                    Err(e) => {
                        self.record_defect(Defect::Phy(PhyFailure::IfaceCreationFailure {
                            phy_id: *ap_phy_id,
                        }))
                        .await;
                        return Err(e);
                    }
                };

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
        let mut result = Ok(());
        let mut failing_phy = None;

        // If the interface has already been destroyed, return Ok.  Only error out in the case that
        // the request to destroy the interface results in a failure.
        for (phy_id, phy_container) in self.phys.iter_mut() {
            if phy_container.ap_ifaces.remove(&iface_id) {
                match destroy_iface(&self.device_monitor, iface_id, &self.telemetry_sender).await {
                    Ok(()) => {}
                    Err(e) => {
                        let _ = phy_container.ap_ifaces.insert(iface_id);
                        result = Err(e);
                        failing_phy = Some(*phy_id);
                    }
                }
                break;
            }
        }

        match (result.as_ref(), failing_phy) {
            (Err(_), Some(phy_id)) => {
                self.record_defect(Defect::Phy(PhyFailure::IfaceDestructionFailure { phy_id }))
                    .await
            }
            _ => {}
        }

        result
    }

    async fn destroy_all_ap_ifaces(&mut self) -> Result<(), PhyManagerError> {
        let ap_capable_phys = self.phys_for_role(fidl_common::WlanMacRole::Ap);
        let mut result = Ok(());
        let mut failing_phys = Vec::new();

        for ap_phy in ap_capable_phys.iter() {
            let mut phy_container =
                self.phys.get_mut(ap_phy).ok_or(PhyManagerError::PhyQueryFailure)?;

            // Continue tracking interface IDs for which deletion fails.
            let mut lingering_ifaces = HashSet::new();
            for iface_id in phy_container.ap_ifaces.drain() {
                match destroy_iface(&self.device_monitor, iface_id, &self.telemetry_sender).await {
                    Ok(()) => {}
                    Err(e) => {
                        result = Err(e);
                        failing_phys.push(ap_phy);
                        let _ = lingering_ifaces.insert(iface_id);
                    }
                }
            }
            phy_container.ap_ifaces = lingering_ifaces;
        }

        if result.is_err() {
            for phy_id in failing_phys {
                self.record_defect(Defect::Phy(PhyFailure::IfaceDestructionFailure {
                    phy_id: *phy_id,
                }))
                .await
            }
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
        // Search phys for a client iface with security support indicating WPA3 support.
        for (_, phy) in self.phys.iter() {
            for (_, security_support) in phy.client_ifaces.iter() {
                if wpa3_supported(*security_support) {
                    return true;
                }
            }
        }
        false
    }

    async fn set_power_state(
        &mut self,
        power_state: fidl_common::PowerSaveType,
    ) -> Result<fuchsia_zircon::Status, anyhow::Error> {
        self.power_state = power_state;
        let mut final_status = fuchsia_zircon::Status::OK;

        for phy_id in self.phys.keys() {
            let result = set_ps_mode(&self.device_monitor, *phy_id, power_state).await?;
            if let Err(status) = fuchsia_zircon::ok(result) {
                final_status = status;
            }
        }

        Ok(final_status)
    }

    async fn record_defect(&mut self, defect: Defect) {
        match defect {
            Defect::Phy(PhyFailure::IfaceCreationFailure { phy_id })
            | Defect::Phy(PhyFailure::IfaceDestructionFailure { phy_id }) => {
                if let Some(container) = self.phys.get_mut(&phy_id) {
                    container.defects.add_event(defect)
                }
            }
            Defect::Iface(IfaceFailure::CanceledScan { iface_id })
            | Defect::Iface(IfaceFailure::FailedScan { iface_id })
            | Defect::Iface(IfaceFailure::EmptyScanResults { iface_id })
            | Defect::Iface(IfaceFailure::ConnectionFailure { iface_id }) => {
                for (_, phy_info) in self.phys.iter_mut() {
                    if phy_info.client_ifaces.contains_key(&iface_id) {
                        phy_info.defects.add_event(defect);
                        break;
                    }
                }
            }
            Defect::Iface(IfaceFailure::ApStartFailure { iface_id }) => {
                for (_, phy_info) in self.phys.iter_mut() {
                    if phy_info.ap_ifaces.contains(&iface_id) {
                        phy_info.defects.add_event(defect);
                        break;
                    }
                }
            }
        }
    }
}

/// Creates an interface of the requested role for the requested PHY ID.  Returns either the
/// ID of the created interface or an error.
async fn create_iface(
    proxy: &fidl_service::DeviceMonitorProxy,
    phy_id: u16,
    role: fidl_common::WlanMacRole,
    sta_addr: MacAddr,
    telemetry_sender: &TelemetrySender,
) -> Result<u16, PhyManagerError> {
    let mut request = fidl_service::CreateIfaceRequest { phy_id, role, sta_addr };
    let create_iface_response = match proxy.create_iface(&mut request).await {
        Ok((status, iface_response)) => {
            if fuchsia_zircon::ok(status).is_err() || iface_response.is_none() {
                telemetry_sender.send(TelemetryEvent::IfaceCreationFailure);
                return Err(PhyManagerError::IfaceCreateFailure);
            }
            iface_response.ok_or_else(|| PhyManagerError::IfaceCreateFailure)?
        }
        Err(e) => {
            warn!("failed to create iface for PHY {}: {}", phy_id, e);
            telemetry_sender.send(TelemetryEvent::IfaceCreationFailure);
            return Err(PhyManagerError::IfaceCreateFailure);
        }
    };
    Ok(create_iface_response.iface_id)
}

/// Destroys the specified interface.
async fn destroy_iface(
    proxy: &fidl_service::DeviceMonitorProxy,
    iface_id: u16,
    telemetry_sender: &TelemetrySender,
) -> Result<(), PhyManagerError> {
    let mut request = fidl_service::DestroyIfaceRequest { iface_id: iface_id };
    match proxy.destroy_iface(&mut request).await {
        Ok(status) => match status {
            fuchsia_zircon::sys::ZX_OK => Ok(()),
            ref e => {
                if *e != fuchsia_zircon::sys::ZX_ERR_NOT_FOUND {
                    telemetry_sender.send(TelemetryEvent::IfaceDestructionFailure);
                }
                warn!("failed to destroy iface {}: {}", iface_id, e);
                Err(PhyManagerError::IfaceDestroyFailure)
            }
        },
        Err(e) => {
            warn!("failed to send destroy iface {}: {}", iface_id, e);
            telemetry_sender.send(TelemetryEvent::IfaceDestructionFailure);
            Err(PhyManagerError::IfaceDestroyFailure)
        }
    }
}

async fn query_security_support(
    proxy: &fidl_service::DeviceMonitorProxy,
    iface_id: u16,
) -> Result<fidl_common::SecuritySupport, PhyManagerError> {
    let (feature_support_proxy, feature_support_server) =
        fidl::endpoints::create_proxy().map_err(|_| PhyManagerError::IfaceQueryFailure)?;
    proxy
        .get_feature_support(iface_id, feature_support_server)
        .await
        .map_err(|_| {
            warn!("Feature support request failed for iface {}", iface_id);
            PhyManagerError::IfaceQueryFailure
        })?
        .map_err(|_| {
            warn!("Feature support queries unavailable for iface {}", iface_id);
            PhyManagerError::IfaceQueryFailure
        })?;
    let result = feature_support_proxy.query_security_support().await.map_err(|_| {
        warn!("Security support query failed for iface {}", iface_id);
        PhyManagerError::IfaceQueryFailure
    })?;
    result.map_err(|_| {
        warn!("Security support unavailable for iface {}", iface_id);
        PhyManagerError::IfaceQueryFailure
    })
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

async fn set_ps_mode(
    proxy: &fidl_service::DeviceMonitorProxy,
    phy_id: u16,
    state: fidl_common::PowerSaveType,
) -> Result<i32, anyhow::Error> {
    let mut req = fidl_service::SetPsModeRequest { phy_id, ps_mode: state };
    proxy.set_ps_mode(&mut req).await.map_err(|e| e.into())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints,
        fidl_fuchsia_wlan_device_service as fidl_service, fidl_fuchsia_wlan_sme as fidl_sme,
        fuchsia_async::{run_singlethreaded, TestExecutor},
        fuchsia_inspect::{self as inspect, assert_data_tree},
        fuchsia_zircon::sys::{ZX_ERR_NOT_FOUND, ZX_OK},
        futures::{channel::mpsc, stream::StreamExt, task::Poll},
        pin_utils::pin_mut,
        test_case::test_case,
        wlan_common::assert_variant,
    };

    /// Hold the client and service ends for DeviceMonitor to allow mocking DeviceMonitor responses
    /// for unit tests.
    struct TestValues {
        monitor_proxy: fidl_service::DeviceMonitorProxy,
        monitor_stream: fidl_service::DeviceMonitorRequestStream,
        inspector: inspect::Inspector,
        node: inspect::Node,
        telemetry_sender: TelemetrySender,
        telemetry_receiver: mpsc::Receiver<TelemetryEvent>,
    }

    /// Create a TestValues for a unit test.
    fn test_setup() -> TestValues {
        let (monitor_proxy, monitor_requests) =
            endpoints::create_proxy::<fidl_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let monitor_stream = monitor_requests.into_stream().expect("failed to create stream");

        let inspector = inspect::Inspector::new();
        let node = inspector.root().create_child("phy_manager");
        let (sender, telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let telemetry_sender = TelemetrySender::new(sender);

        TestValues {
            monitor_proxy,
            monitor_stream,
            inspector,
            node,
            telemetry_sender,
            telemetry_receiver,
        }
    }

    /// Take in the service side of a DeviceMonitor::GetSupportedMacRoles request and respond with
    /// the given WlanMacRoles responst.
    fn send_get_supported_mac_roles_response(
        exec: &mut TestExecutor,
        server: &mut fidl_service::DeviceMonitorRequestStream,
        mut supported_mac_roles: Result<
            Vec<fidl_common::WlanMacRole>,
            fuchsia_zircon::sys::zx_status_t,
        >,
    ) {
        let _ = assert_variant!(
            exec.run_until_stalled(&mut server.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceMonitorRequest::GetSupportedMacRoles {
                    responder, ..
                }
            ))) => {
                responder.send(&mut supported_mac_roles)
            }
        );
    }

    /// Create a PhyInfo object for unit testing.
    #[track_caller]
    fn send_query_iface_response(
        exec: &mut TestExecutor,
        server: &mut fidl_service::DeviceMonitorRequestStream,
        iface_info: Option<fidl_service::QueryIfaceResponse>,
    ) {
        let mut response = iface_info.ok_or(ZX_ERR_NOT_FOUND);
        assert_variant!(
            exec.run_until_stalled(&mut server.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceMonitorRequest::QueryIface {
                    iface_id: _,
                    responder,
                }
            ))) => {
                responder.send(&mut response).expect("sending fake iface info");
            }
        );
    }

    /// Serve a request for feature support for unit testing.
    #[track_caller]
    fn serve_feature_support(
        exec: &mut TestExecutor,
        server: &mut fidl_service::DeviceMonitorRequestStream,
    ) -> fidl_sme::FeatureSupportRequestStream {
        let feature_support_server = assert_variant!(
            exec.run_until_stalled(&mut server.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceMonitorRequest::GetFeatureSupport {
                    iface_id: _,
                    feature_support_server,
                    responder,
                }
            ))) => {
                responder.send(&mut Ok(())).expect("sending feature support response");
                feature_support_server
            }
        );
        feature_support_server.into_stream().expect("extracting feature support request stream")
    }

    /// Serve the given security support for unit testing.
    #[track_caller]
    fn send_security_support(
        exec: &mut TestExecutor,
        stream: &mut fidl_sme::FeatureSupportRequestStream,
        security_support: Option<fidl_common::SecuritySupport>,
    ) {
        let mut response = security_support.ok_or(ZX_ERR_NOT_FOUND);
        assert_variant!(
            exec.run_until_stalled(&mut stream.next()),
            Poll::Ready(Some(Ok(
                fidl_sme::FeatureSupportRequest::QuerySecuritySupport {
                    responder,
                }
            ))) => {
                responder.send(&mut response).expect("sending fake security support");
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

    /// Creates a QueryIfaceResponse from the arguments provided by the caller.
    fn create_iface_response(
        role: fidl_common::WlanMacRole,
        id: u16,
        phy_id: u16,
        phy_assigned_id: u16,
        mac: [u8; 6],
    ) -> fidl_service::QueryIfaceResponse {
        fidl_service::QueryIfaceResponse {
            role: role,
            id: id,
            phy_id: phy_id,
            phy_assigned_id: phy_assigned_id,
            sta_addr: mac,
        }
    }

    /// Create an empty security support structure.
    fn fake_security_support() -> fidl_common::SecuritySupport {
        fidl_common::SecuritySupport {
            mfp: fidl_common::MfpFeature { supported: false },
            sae: fidl_common::SaeFeature {
                driver_handler_supported: false,
                sme_handler_supported: false,
            },
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
        let fake_mac_roles = vec![];

        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );
        {
            let add_phy_fut = phy_manager.add_phy(0);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Ok(fake_mac_roles.clone()),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));
        assert_eq!(
            phy_manager.phys.get(&fake_phy_id).unwrap().supported_mac_roles,
            fake_mac_roles.into_iter().collect()
        );
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnPhyCreated event and
    /// calling add_phy on PhyManager for a PHY that does not exist.  The PhyManager in this case
    /// should not create and store a new PhyContainer.
    #[fuchsia::test]
    fn add_invalid_phy() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        {
            let add_phy_fut = phy_manager.add_phy(1);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND),
            );

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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        let fake_phy_id = 0;
        let fake_mac_roles = vec![];

        {
            let add_phy_fut = phy_manager.add_phy(fake_phy_id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Ok(fake_mac_roles.clone()),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        {
            assert!(phy_manager.phys.contains_key(&fake_phy_id));
            assert_eq!(
                phy_manager.phys.get(&fake_phy_id).unwrap().supported_mac_roles,
                fake_mac_roles.clone().into_iter().collect()
            );
        }

        // Send an update for the same PHY ID and ensure that the PHY info is updated.
        {
            let add_phy_fut = phy_manager.add_phy(fake_phy_id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Ok(fake_mac_roles.clone()),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));
        assert_eq!(
            phy_manager.phys.get(&fake_phy_id).unwrap().supported_mac_roles,
            fake_mac_roles.into_iter().collect()
        );
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnPhyRemoved event and
    /// calling remove_phy on PhyManager for a PHY that not longer exists.  The PhyManager in this
    /// case should remove the PhyContainer associated with the removed PHY ID.
    #[fuchsia::test]
    fn add_phy_after_create_all_client_ifaces() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];

        {
            let start_connections_fut = phy_manager
                .create_all_client_ifaces(CreateClientIfacesReason::StartClientConnections);
            pin_mut!(start_connections_fut);
            assert!(exec.run_until_stalled(&mut start_connections_fut).is_ready());
        }

        // Add a new phy.  Since client connections have been started, it should also create a
        // client iface.
        {
            let add_phy_fut = phy_manager.add_phy(fake_phy_id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Ok(fake_mac_roles),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_create_iface_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Some(fake_iface_id),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            let mut feature_support_stream =
                serve_feature_support(&mut exec, &mut test_values.monitor_stream);

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_security_support(
                &mut exec,
                &mut feature_support_stream,
                Some(fake_security_support()),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));
        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.client_ifaces.contains_key(&fake_iface_id));
        assert_eq!(phy_container.client_ifaces.get(&fake_iface_id), Some(&fake_security_support()));
        assert!(phy_container.defects.events.is_empty());
    }

    /// Tests the case where a PHY is added after client connections have been enabled but creating
    /// an interface for the new PHY fails.  In this case, the PHY is not added.
    ///
    /// If this behavior changes, defect accounting needs to be updated and tested here.
    #[fuchsia::test]
    fn add_phy_with_iface_creation_failure() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];

        {
            let start_connections_fut = phy_manager
                .create_all_client_ifaces(CreateClientIfacesReason::StartClientConnections);
            pin_mut!(start_connections_fut);
            assert!(exec.run_until_stalled(&mut start_connections_fut).is_ready());
        }

        // Add a new phy.  Since client connections have been started, it should also create a
        // client iface.
        {
            let add_phy_fut = phy_manager.add_phy(fake_phy_id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Ok(fake_mac_roles),
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            // Send back an error to mimic a failure to create an interface.
            send_create_iface_response(&mut exec, &mut test_values.monitor_stream, None);

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        assert!(!phy_manager.phys.contains_key(&fake_phy_id));
    }

    /// Tests the case where a new PHY is discovered after the country code has been set.
    #[fuchsia::test]
    fn test_add_phy_after_setting_country_code() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        let fake_phy_id = 1;
        let fake_mac_roles = vec![];

        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        {
            let set_country_fut = phy_manager.set_country_code(Some([0, 1]));
            pin_mut!(set_country_fut);
            assert_variant!(exec.run_until_stalled(&mut set_country_fut), Poll::Ready(Ok(())));
        }

        {
            let add_phy_fut = phy_manager.add_phy(fake_phy_id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Ok(fake_mac_roles.clone()),
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
        assert_eq!(
            phy_manager.phys.get(&fake_phy_id).unwrap().supported_mac_roles,
            fake_mac_roles.into_iter().collect()
        );
    }

    #[run_singlethreaded(test)]
    async fn remove_valid_phy() {
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        let fake_phy_id = 1;
        let fake_mac_roles = vec![];

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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        let fake_phy_id = 1;
        let fake_mac_roles = vec![];

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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![];

        let phy_container = PhyContainer::new(fake_mac_roles);

        // Create an IfaceResponse to be sent to the PhyManager when the iface ID is queried
        let fake_role = fidl_common::WlanMacRole::Client;
        let fake_iface_id = 1;
        let fake_phy_assigned_id = 1;
        let fake_sta_addr = [0, 1, 2, 3, 4, 5];
        let iface_response = create_iface_response(
            fake_role,
            fake_iface_id,
            fake_phy_id,
            fake_phy_assigned_id,
            fake_sta_addr,
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
                &mut test_values.monitor_stream,
                Some(iface_response),
            );

            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            let mut feature_support_stream =
                serve_feature_support(&mut exec, &mut test_values.monitor_stream);

            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_security_support(
                &mut exec,
                &mut feature_support_stream,
                Some(fake_security_support()),
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![];

        // Create an IfaceResponse to be sent to the PhyManager when the iface ID is queried
        let fake_role = fidl_common::WlanMacRole::Client;
        let fake_iface_id = 1;
        let fake_phy_assigned_id = 1;
        let fake_sta_addr = [0, 1, 2, 3, 4, 5];
        let iface_response = create_iface_response(
            fake_role,
            fake_iface_id,
            fake_phy_id,
            fake_phy_assigned_id,
            fake_sta_addr,
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
                &mut test_values.monitor_stream,
                Some(iface_response),
            );

            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            let mut feature_support_stream =
                serve_feature_support(&mut exec, &mut test_values.monitor_stream);

            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_security_support(
                &mut exec,
                &mut feature_support_stream,
                Some(fake_security_support()),
            );

            // And then the PHY information is queried.
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Ok(fake_mac_roles),
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![];

        // Inject the fake PHY information
        let phy_container = PhyContainer::new(fake_mac_roles);
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Create an IfaceResponse to be sent to the PhyManager when the iface ID is queried
        let fake_role = fidl_common::WlanMacRole::Client;
        let fake_iface_id = 1;
        let fake_phy_assigned_id = 1;
        let fake_sta_addr = [0, 1, 2, 3, 4, 5];
        let iface_response = create_iface_response(
            fake_role,
            fake_iface_id,
            fake_phy_id,
            fake_phy_assigned_id,
            fake_sta_addr,
        );

        // Add the same iface ID twice
        for _ in 0..2 {
            // Add the fake iface
            let on_iface_added_fut = phy_manager.on_iface_added(fake_iface_id);
            pin_mut!(on_iface_added_fut);
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_query_iface_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Some(iface_response),
            );

            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            let mut feature_support_stream =
                serve_feature_support(&mut exec, &mut test_values.monitor_stream);

            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_security_support(
                &mut exec,
                &mut feature_support_stream,
                Some(fake_security_support()),
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        {
            // Add the non-existent iface
            let on_iface_added_fut = phy_manager.on_iface_added(1);
            pin_mut!(on_iface_added_fut);
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_query_iface_response(&mut exec, &mut test_values.monitor_stream, None);

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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![];

        // Inject the fake PHY information
        let mut phy_container = PhyContainer::new(fake_mac_roles);
        let fake_iface_id = 1;
        let _ = phy_container.client_ifaces.insert(fake_iface_id, fake_security_support());

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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![];

        let present_iface_id = 1;
        let removed_iface_id = 2;

        // Inject the fake PHY information
        let mut phy_container = PhyContainer::new(fake_mac_roles);
        let _ = phy_container.client_ifaces.insert(present_iface_id, fake_security_support());
        let _ = phy_container.client_ifaces.insert(removed_iface_id, fake_security_support());
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        let client = phy_manager.get_client();
        assert!(client.is_none());
    }

    /// Tests the response of the PhyManager when a client iface is requested, a client-capable PHY
    /// has been discovered, but client connections have not been started.  The expectation is that
    /// the PhyManager returns None.
    #[run_singlethreaded(test)]
    async fn get_unconfigured_client() {
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );
        phy_manager.client_connections_enabled = true;

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];
        let phy_container = PhyContainer::new(fake_mac_roles);

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Insert the fake iface
        let fake_iface_id = 1;
        let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
        let _ = phy_container.client_ifaces.insert(fake_iface_id, fake_security_support());

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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Ap];
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Ap];
        let mut phy_container = PhyContainer::new(fake_mac_roles);
        let _ = phy_container.ap_ifaces.insert(1);
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);
        // Create a PhyContainer that has a client iface but no WPA3 support.
        let fake_phy_id_client = 2;
        let fake_mac_roles_client = vec![fidl_common::WlanMacRole::Client];
        let mut phy_container_client = PhyContainer::new(fake_mac_roles_client);
        let _ = phy_container_client.client_ifaces.insert(2, fake_security_support());
        let _ = phy_manager.phys.insert(fake_phy_id_client, phy_container_client);

        // Retrieve the client ID
        let client = phy_manager.get_wpa3_capable_client();
        assert_eq!(client, None);
    }

    /// Tests the response of the PhyManager when a wpa3 capable client iface is requested and
    /// a matching client iface is present.  The expectation is that the PhyManager should reply
    /// with the iface ID of the client iface.
    #[test_case(true, true, false)]
    #[test_case(true, false, true)]
    #[test_case(true, true, true)]
    #[fuchsia::test(add_test_attr = false)]
    fn get_configured_wpa3_client(
        mfp_supported: bool,
        sae_driver_handler_supported: bool,
        sae_sme_handler_supported: bool,
    ) {
        let mut security_support = fake_security_support();
        security_support.mfp.supported = mfp_supported;
        security_support.sae.driver_handler_supported = sae_driver_handler_supported;
        security_support.sae.sme_handler_supported = sae_sme_handler_supported;
        let _exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer with WPA3 support to be inserted into the test
        // PhyManager
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];
        let mut phy_container = PhyContainer::new(fake_mac_roles);
        // Insert the fake iface
        let fake_iface_id = 1;

        let _ = phy_container.client_ifaces.insert(fake_iface_id, security_support);

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Retrieve the client ID
        let client = phy_manager.get_wpa3_capable_client();
        assert_eq!(client.unwrap(), fake_iface_id)
    }

    /// Tests that PhyManager will not return a client interface when client connections are not
    /// enabled.
    #[fuchsia::test]
    fn get_client_while_stopped() {
        let _exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Create a new PhyManager.  On construction, client connections are disabled.
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );
        assert!(!phy_manager.client_connections_enabled);

        // Add a PHY with a lingering client interface.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];
        let mut phy_container = PhyContainer::new(fake_mac_roles);
        let _ = phy_container.client_ifaces.insert(1, fake_security_support());
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Try to get a client interface.  No interface should be returned since client connections
        // are disabled.
        assert_eq!(phy_manager.get_client(), None);
    }

    /// Tests the PhyManager's response to stop_client_connection when there is an existing client
    /// iface.  The expectation is that the client iface is destroyed and there is no remaining
    /// record of the iface ID in the PhyManager.
    #[fuchsia::test]
    fn destroy_all_client_ifaces() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];
        let phy_container = PhyContainer::new(fake_mac_roles);

        {
            let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            let _ = phy_container.client_ifaces.insert(fake_iface_id, fake_security_support());

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

        // Verify that the client_connections_enabled has been set to false.
        assert!(!phy_manager.client_connections_enabled);
    }

    /// Tests the PhyManager's response to destroy_all_client_ifaces when no client ifaces are
    /// present but an AP iface is present.  The expectation is that the AP iface is left intact.
    #[fuchsia::test]
    fn destroy_all_client_ifaces_no_clients() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Ap];
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

    /// This test validates the behavior when stopping client connections fails.
    #[fuchsia::test]
    fn destroy_all_client_ifaces_fails() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Drop the monitor stream so that the request to destroy the interface fails.
        drop(test_values.monitor_stream);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];
        let mut phy_container = PhyContainer::new(fake_mac_roles);

        // For the sake of this test, force the retention period to be indefinite to make sure
        // that an event is logged.
        phy_container.defects = EventHistory::<Defect>::new(u32::MAX);

        {
            let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            let _ = phy_container.client_ifaces.insert(fake_iface_id, fake_security_support());

            // Stop client connections and expect the future to fail immediately.
            let stop_clients_future = phy_manager.destroy_all_client_ifaces();
            pin_mut!(stop_clients_future);
            assert!(exec.run_until_stalled(&mut stop_clients_future).is_ready());
        }

        // Ensure that the client interface is still present
        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.client_ifaces.contains_key(&fake_iface_id));
        assert_eq!(phy_container.defects.events.len(), 1);
        assert_eq!(
            phy_container.defects.events[0].value,
            Defect::Phy(PhyFailure::IfaceDestructionFailure { phy_id: 1 })
        );
    }

    /// Tests the PhyManager's response to a request for an AP when no PHYs are present.  The
    /// expectation is that the PhyManager will return None in this case.
    #[fuchsia::test]
    fn get_ap_no_phys() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Ap];
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

    /// Tests the case where an AP interface is requested but interface creation fails.
    #[fuchsia::test]
    fn get_ap_iface_creation_fails() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Drop the monitor stream so that the request to destroy the interface fails.
        drop(test_values.monitor_stream);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Ap];
        let mut phy_container = PhyContainer::new(fake_mac_roles.clone());

        // For the sake of this test, force the retention period to be indefinite to make sure
        // that an event is logged.
        phy_container.defects = EventHistory::<Defect>::new(u32::MAX);

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        {
            let get_ap_future = phy_manager.create_or_get_ap_iface();

            pin_mut!(get_ap_future);
            assert!(exec.run_until_stalled(&mut get_ap_future).is_ready());
        }

        assert!(phy_manager.phys[&fake_phy_id].ap_ifaces.is_empty());
        assert_eq!(phy_manager.phys[&fake_phy_id].defects.events.len(), 1);
        assert_eq!(
            phy_manager.phys[&fake_phy_id].defects.events[0].value,
            Defect::Phy(PhyFailure::IfaceCreationFailure { phy_id: 1 })
        );
    }

    /// Tests the PhyManager's response to a create_or_get_ap_iface call when there is a PHY with an AP iface
    /// that has already been created.  The expectation is that the PhyManager should return the
    /// iface ID of the existing AP iface.
    #[fuchsia::test]
    fn get_configured_ap() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Ap];
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Ap];

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
        assert!(phy_container.defects.events.is_empty());
    }

    /// This test attempts to stop an invalid AP iface ID.  The expectation is that a valid iface
    /// ID is unaffected.
    #[fuchsia::test]
    fn stop_invalid_ap_iface() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Ap];

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
        assert!(phy_container.defects.events.is_empty());
    }

    /// This test fails to stop a valid AP iface on a PhyManager.  The expectation is that the
    /// PhyManager should retain the AP interface and log a defect.
    #[fuchsia::test]
    fn stop_ap_iface_fails() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Drop the monitor stream so that the request to destroy the interface fails.
        drop(test_values.monitor_stream);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Ap];

        {
            let mut phy_container = PhyContainer::new(fake_mac_roles.clone());

            // For the sake of this test, force the retention period to be indefinite to make sure
            // that an event is logged.
            phy_container.defects = EventHistory::<Defect>::new(u32::MAX);

            let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            let _ = phy_container.ap_ifaces.insert(fake_iface_id);

            // Remove the AP iface ID
            let destroy_ap_iface_future = phy_manager.destroy_ap_iface(fake_iface_id);
            pin_mut!(destroy_ap_iface_future);
            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.ap_ifaces.contains(&fake_iface_id));
        assert_eq!(phy_container.defects.events.len(), 1);
        assert_eq!(
            phy_container.defects.events[0].value,
            Defect::Phy(PhyFailure::IfaceDestructionFailure { phy_id: 1 })
        );
    }

    /// This test attempts to stop an invalid AP iface ID.  The expectation is that a valid iface
    /// This test creates two AP ifaces for a PHY that supports AP ifaces.  destroy_all_ap_ifaces is then
    /// called on the PhyManager.  The expectation is that both AP ifaces should be destroyed and
    /// the records of the iface IDs should be removed from the PhyContainer.
    #[fuchsia::test]
    fn stop_all_ap_ifaces() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // ifaces are added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Ap];

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
        assert!(phy_container.defects.events.is_empty());
    }

    /// This test calls destroy_all_ap_ifaces on a PhyManager that only has a client iface.  The expectation
    /// is that no interfaces should be destroyed and the client iface ID should remain in the
    /// PhyManager
    #[fuchsia::test]
    fn stop_all_ap_ifaces_with_client() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];

        {
            let phy_container = PhyContainer::new(fake_mac_roles);

            let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            let _ = phy_container.client_ifaces.insert(fake_iface_id, fake_security_support());

            // Stop all AP ifaces
            let destroy_ap_iface_future = phy_manager.destroy_all_ap_ifaces();
            pin_mut!(destroy_ap_iface_future);
            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.client_ifaces.contains_key(&fake_iface_id));
        assert!(phy_container.defects.events.is_empty());
    }

    /// This test validates the behavior when destroying all AP interfaces fails.
    #[fuchsia::test]
    fn stop_all_ap_ifaces_fails() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Drop the monitor stream so that the request to destroy the interface fails.
        drop(test_values.monitor_stream);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // ifaces are added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Ap];

        {
            let mut phy_container = PhyContainer::new(fake_mac_roles.clone());

            // For the sake of this test, force the retention period to be indefinite to make sure
            // that an event is logged.
            phy_container.defects = EventHistory::<Defect>::new(u32::MAX);

            let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            let _ = phy_container.ap_ifaces.insert(0);
            let _ = phy_container.ap_ifaces.insert(1);

            // Expect interface destruction to finish immediately.
            let destroy_ap_iface_future = phy_manager.destroy_all_ap_ifaces();
            pin_mut!(destroy_ap_iface_future);
            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert_eq!(phy_container.ap_ifaces.len(), 2);
        assert_eq!(phy_container.defects.events.len(), 2);
        assert_eq!(
            phy_container.defects.events[0].value,
            Defect::Phy(PhyFailure::IfaceDestructionFailure { phy_id: 1 })
        );
        assert_eq!(
            phy_container.defects.events[1].value,
            Defect::Phy(PhyFailure::IfaceDestructionFailure { phy_id: 1 })
        );
    }

    /// Verifies that setting a suggested AP MAC address results in that MAC address being used as
    /// a part of the request to create an AP interface.  Ensures that this does not affect client
    /// interface requests.
    #[fuchsia::test]
    fn test_suggest_ap_mac() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Ap];
        let phy_container = PhyContainer::new(fake_mac_roles.clone());

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Insert the fake iface
        let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
        let _ = phy_container.client_ifaces.insert(fake_iface_id, fake_security_support());

        // Suggest an AP MAC
        let mac = MacAddress::from_bytes(&[1, 2, 3, 4, 5, 6]).unwrap();
        phy_manager.suggest_ap_mac(mac);

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
                let requested_mac = MacAddress::from_bytes(&req.sta_addr).unwrap();
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];
        let phy_container = PhyContainer::new(fake_mac_roles.clone());

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Suggest an AP MAC
        let mac = MacAddress::from_bytes(&[1, 2, 3, 4, 5, 6]);
        phy_manager.suggest_ap_mac(mac.unwrap());

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
                assert_eq!(req.sta_addr, ieee80211::NULL_MAC_ADDR);
                let mut response = fidl_service::CreateIfaceResponse { iface_id: fake_iface_id };
                let response = Some(&mut response);
                responder.send(ZX_OK, response).expect("sending fake iface id");
            }
        );
        // Expect an iface security support query, and send back a response
        assert_variant!(exec.run_until_stalled(&mut start_client_future), Poll::Pending);

        let mut feature_support_stream =
            serve_feature_support(&mut exec, &mut test_values.monitor_stream);

        assert!(exec.run_until_stalled(&mut start_client_future).is_pending());

        send_security_support(
            &mut exec,
            &mut feature_support_stream,
            Some(fake_security_support()),
        );

        assert_variant!(exec.run_until_stalled(&mut start_client_future), Poll::Ready(_));
    }

    /// Tests the case where creating a client interface fails while starting client connections.
    #[fuchsia::test]
    fn test_iface_creation_fails_during_start_client_connections() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Drop the monitor stream so that the request to create the interface fails.
        drop(test_values.monitor_stream);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];
        let mut phy_container = PhyContainer::new(fake_mac_roles.clone());

        // For the sake of this test, force the retention period to be indefinite to make sure
        // that an event is logged.
        phy_container.defects = EventHistory::<Defect>::new(u32::MAX);

        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        {
            // Start client connections so that an IfaceRequest is issued for the client.
            let start_client_future = phy_manager
                .create_all_client_ifaces(CreateClientIfacesReason::StartClientConnections);
            pin_mut!(start_client_future);
            assert!(exec.run_until_stalled(&mut start_client_future).is_ready());
        }

        // Verify that a defect has been logged.
        assert_eq!(phy_manager.phys[&fake_phy_id].defects.events.len(), 1);
        assert_eq!(
            phy_manager.phys[&fake_phy_id].defects.events[0].value,
            Defect::Phy(PhyFailure::IfaceCreationFailure { phy_id: 1 })
        );
    }

    /// Tests get_phy_ids() when no PHYs are present. The expectation is that the PhyManager will
    /// Tests get_phy_ids() when no PHYs are present. The expectation is that the PhyManager will
    /// return an empty `Vec` in this case.
    #[run_singlethreaded(test)]
    async fn get_phy_ids_no_phys() {
        let test_values = test_setup();
        let phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );
        assert_eq!(phy_manager.get_phy_ids(), Vec::<u16>::new());
    }

    /// Tests get_phy_ids() when a single PHY is present. The expectation is that the PhyManager will
    /// return a single element `Vec`, with the appropriate ID.
    #[fuchsia::test]
    fn get_phy_ids_single_phy() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        {
            let add_phy_fut = phy_manager.add_phy(1);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());
            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Ok(vec![]),
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        {
            let add_phy_fut = phy_manager.add_phy(1);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());
            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Ok(vec![]),
            );
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        {
            let add_phy_fut = phy_manager.add_phy(2);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());
            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Ok(vec![]),
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Insert a couple fake PHYs.
        let _ = phy_manager.phys.insert(
            0,
            PhyContainer {
                supported_mac_roles: HashSet::new(),
                client_ifaces: HashMap::new(),
                ap_ifaces: HashSet::new(),
                defects: EventHistory::new(DEFECT_RETENTION_SECONDS),
            },
        );
        let _ = phy_manager.phys.insert(
            1,
            PhyContainer {
                supported_mac_roles: HashSet::new(),
                client_ifaces: HashMap::new(),
                ap_ifaces: HashSet::new(),
                defects: EventHistory::new(DEFECT_RETENTION_SECONDS),
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Insert a fake PHY.
        let _ = phy_manager.phys.insert(
            0,
            PhyContainer {
                supported_mac_roles: HashSet::new(),
                client_ifaces: HashMap::new(),
                ap_ifaces: HashSet::new(),
                defects: EventHistory::new(DEFECT_RETENTION_SECONDS),
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];

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
                let _ = phy_container.client_ifaces.insert(phy_id, fake_security_support());
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

                    assert_variant!(exec.run_until_stalled(&mut recovery_fut), Poll::Pending);

                    let mut feature_support_stream =
                        serve_feature_support(&mut exec, &mut test_values.monitor_stream);

                    assert!(exec.run_until_stalled(&mut recovery_fut).is_pending());

                    send_security_support(
                        &mut exec,
                        &mut feature_support_stream,
                        Some(fake_security_support()),
                    );
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];

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

                // If creating the iface succeeded, respond to the security support query.
                if let Some(_iface_id) = created_iface_id {
                    assert_variant!(exec.run_until_stalled(&mut recovery_fut), Poll::Pending);

                    let mut feature_support_stream =
                        serve_feature_support(&mut exec, &mut test_values.monitor_stream);

                    assert!(exec.run_until_stalled(&mut recovery_fut).is_pending());

                    send_security_support(
                        &mut exec,
                        &mut feature_support_stream,
                        Some(fake_security_support()),
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
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Create a fake PHY entry without client interfaces.  Note that client connections have
        // not been set to enabled.
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];
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

    /// Tests the case where client connections are re-started following an unsuccessful stop
    /// client connections request.
    #[fuchsia::test]
    fn test_start_after_unsuccessful_stop() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Verify that client connections are initially stopped.
        assert!(!phy_manager.client_connections_enabled);

        // Create a PHY with a lingering client interface.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_common::WlanMacRole::Client];
        let mut phy_container = PhyContainer::new(fake_mac_roles);
        // Insert the fake iface
        let fake_iface_id = 1;
        let _ = phy_container.client_ifaces.insert(fake_iface_id, fake_security_support());
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        // Try creating all client interfaces due to recovery and ensure that no interfaces are
        // returned.
        {
            let start_client_future =
                phy_manager.create_all_client_ifaces(CreateClientIfacesReason::RecoverClientIfaces);
            pin_mut!(start_client_future);
            assert_variant!(
                exec.run_until_stalled(&mut start_client_future),
                Poll::Ready(Ok(vec)) => {
                assert!(vec.is_empty())
            });
        }

        // Create all client interfaces with the reason set to StartClientConnections and verify
        // that the existing interface is returned.
        {
            let start_client_future = phy_manager
                .create_all_client_ifaces(CreateClientIfacesReason::StartClientConnections);
            pin_mut!(start_client_future);
            assert_variant!(
                exec.run_until_stalled(&mut start_client_future),
                Poll::Ready(Ok(vec)) => {
                assert_eq!(vec, vec![1])
            });
        }
    }

    #[test_case(true, false)]
    #[test_case(false, true)]
    #[test_case(true, true)]
    #[fuchsia::test(add_test_attr = false)]
    fn has_wpa3_client_iface(driver_handler_supported: bool, sme_handler_supported: bool) {
        let _exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Create a phy with the security features that indicate WPA3 support.
        let mut security_support = fake_security_support();
        security_support.mfp.supported = true;
        security_support.sae.driver_handler_supported = driver_handler_supported;
        security_support.sae.sme_handler_supported = sme_handler_supported;
        let fake_phy_id = 0;

        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );
        let mut phy_container = PhyContainer::new(vec![]);
        let _ = phy_container.client_ifaces.insert(0, security_support);
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
        // Create a phy without security features that indicate WPA3 support.
        let fake_phy_id = 0;
        let mut phy_container = PhyContainer::new(vec![]);
        let _ = phy_container.client_ifaces.insert(0, fake_security_support());

        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );
        let _ = phy_manager.phys.insert(fake_phy_id, phy_container);

        assert_eq!(phy_manager.has_wpa3_client_iface(), false);
    }

    /// Tests reporting of client connections status when client connections are enabled.
    #[fuchsia::test]
    fn test_client_connections_enabled_when_enabled() {
        let _exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        phy_manager.client_connections_enabled = true;
        assert!(phy_manager.client_connections_enabled());
    }

    /// Tests reporting of client connections status when client connections are disabled.
    #[fuchsia::test]
    fn test_client_connections_enabled_when_disabled() {
        let _exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        phy_manager.client_connections_enabled = false;
        assert!(!phy_manager.client_connections_enabled());
    }

    /// Tests the case where setting low power state succeeds.
    #[fuchsia::test]
    fn test_succeed_in_setting_power_state() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        assert_eq!(phy_manager.power_state, fidl_common::PowerSaveType::PsModePerformance);

        // Add a couple of PHYs.
        let mut phy_ids = HashSet::<u16>::new();
        let _ = phy_ids.insert(0);
        let _ = phy_ids.insert(1);
        for id in phy_ids.iter() {
            let phy_container = PhyContainer::new(vec![]);
            let _ = phy_manager.phys.insert(*id, phy_container);
        }

        {
            // Set low power state on the PHYs.
            let fut = phy_manager.set_power_state(fidl_common::PowerSaveType::PsModeLowPower);

            // The future should run until it stalls out requesting that one of the PHYs set its low
            // power mode.
            pin_mut!(fut);

            for _ in 0..phy_ids.len() {
                assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
                assert_variant!(
                    exec.run_until_stalled(&mut test_values.monitor_stream.next()),
                    Poll::Ready(Some(Ok(
                        fidl_service::DeviceMonitorRequest::SetPsMode {
                            req: fidl_service::SetPsModeRequest { phy_id, ps_mode },
                            responder,
                        }
                    ))) => {
                        assert!(phy_ids.remove(&phy_id));
                        assert_eq!(ps_mode, fidl_common::PowerSaveType::PsModeLowPower);
                        responder.send(ZX_OK).expect("sending fake set PS mode response");
                    }
                )
            }

            assert_variant!(
                exec.run_until_stalled(&mut fut),
                Poll::Ready(Ok(fuchsia_zircon::Status::OK))
            );
        }
        assert_eq!(phy_manager.power_state, fidl_common::PowerSaveType::PsModeLowPower);
    }

    /// Tests the case where setting low power state fails.
    #[fuchsia::test]
    fn test_fail_to_set_power_state() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        assert_eq!(phy_manager.power_state, fidl_common::PowerSaveType::PsModePerformance);

        // Add a couple of PHYs.
        let mut phy_ids = HashSet::<u16>::new();
        let _ = phy_ids.insert(0);
        let _ = phy_ids.insert(1);
        for id in phy_ids.iter() {
            let phy_container = PhyContainer::new(vec![]);
            let _ = phy_manager.phys.insert(*id, phy_container);
        }

        {
            // Set low power state on the PHYs.
            let fut = phy_manager.set_power_state(fidl_common::PowerSaveType::PsModeLowPower);

            // The future should run until it stalls out requesting that one of the PHYs set its low
            // power mode.
            pin_mut!(fut);

            for _ in 0..phy_ids.len() {
                assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
                assert_variant!(
                    exec.run_until_stalled(&mut test_values.monitor_stream.next()),
                    Poll::Ready(Some(Ok(
                        fidl_service::DeviceMonitorRequest::SetPsMode {
                            req: fidl_service::SetPsModeRequest { phy_id, ps_mode },
                            responder,
                        }
                    ))) => {
                        assert!(phy_ids.remove(&phy_id));
                        assert_eq!(ps_mode, fidl_common::PowerSaveType::PsModeLowPower);

                        // Send back a failure for one of the PHYs
                        if phy_id == 0 {
                            responder.send(ZX_OK).expect("sending fake set PS mode response");
                        } else {
                            responder
                                .send(ZX_ERR_NOT_FOUND)
                                .expect("sending negativefake set PS mode response");
                        }
                    }
                )
            }

            // An error should be reported due to the failure to set the power mode on one of the
            // PHYs.
            assert_variant!(
                exec.run_until_stalled(&mut fut),
                Poll::Ready(Ok(fuchsia_zircon::Status::NOT_FOUND))
            );
        }
        assert_eq!(phy_manager.power_state, fidl_common::PowerSaveType::PsModeLowPower);
    }

    /// Tests the case where the request cannot be made to configure low power mode for a PHY.
    #[fuchsia::test]
    fn test_fail_to_request_low_power_mode() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Drop the receiving end of the device monitor channel.
        drop(test_values.monitor_stream);

        // Add a couple of PHYs.
        let mut phy_ids = HashSet::<u16>::new();
        let _ = phy_ids.insert(0);
        let _ = phy_ids.insert(1);
        for id in phy_ids.iter() {
            let phy_container = PhyContainer::new(vec![]);
            let _ = phy_manager.phys.insert(*id, phy_container);
        }

        // Set low power state on the PHYs.
        let fut = phy_manager.set_power_state(fidl_common::PowerSaveType::PsModePerformance);

        // The future should run until it stalls out requesting that one of the PHYs set its low
        // power mode.
        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where a PHY is added after the low power state has been enabled.
    #[fuchsia::test]
    fn test_add_phy_after_low_power_enabled() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        assert_eq!(phy_manager.power_state, fidl_common::PowerSaveType::PsModePerformance);

        // Enable low power mode which should complete immediately.
        {
            let fut = phy_manager.set_power_state(fidl_common::PowerSaveType::PsModeBalanced);
            pin_mut!(fut);
            assert_variant!(
                exec.run_until_stalled(&mut fut),
                Poll::Ready(Ok(fuchsia_zircon::Status::OK))
            );
        }

        assert_eq!(phy_manager.power_state, fidl_common::PowerSaveType::PsModeBalanced);

        // Add a new PHY and ensure that the low power mode is set
        {
            let add_phy_fut = phy_manager.add_phy(0);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_get_supported_mac_roles_response(
                &mut exec,
                &mut test_values.monitor_stream,
                Ok(Vec::new()),
            );

            // There should be a stall as the low power mode is requested.
            assert_variant!(exec.run_until_stalled(&mut add_phy_fut), Poll::Pending);
            assert_variant!(
                exec.run_until_stalled(&mut test_values.monitor_stream.next()),
                Poll::Ready(Some(Ok(
                    fidl_service::DeviceMonitorRequest::SetPsMode {
                        req: fidl_service::SetPsModeRequest { phy_id, ps_mode },
                        responder,
                    }
                ))) => {
                    assert_eq!(ps_mode, fidl_common::PowerSaveType::PsModeBalanced);

                    // Send back a failure for one of the PHYs
                    if phy_id == 0 {
                        responder.send(ZX_OK).expect("sending fake set PS mode response");
                    } else {
                        responder
                            .send(ZX_ERR_NOT_FOUND)
                            .expect("sending negativefake set PS mode response");
                    }
                }
            );

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }
    }

    #[fuchsia::test]
    fn test_create_iface_succeeds() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Issue a create iface request
        let fut = create_iface(
            &mut test_values.monitor_proxy,
            0,
            fidl_common::WlanMacRole::Client,
            NULL_MAC_ADDR,
            &test_values.telemetry_sender,
        );
        pin_mut!(fut);

        // Wait for the request to stall out waiting for DeviceMonitor.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Send back a positive response from DeviceMonitor.
        send_create_iface_response(&mut exec, &mut test_values.monitor_stream, Some(0));

        // The future should complete.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(0)));

        // Verify that there is nothing waiting on the telemetry receiver.
        assert_variant!(test_values.telemetry_receiver.try_next(), Err(_))
    }

    #[fuchsia::test]
    fn test_create_iface_fails() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Issue a create iface request
        let fut = create_iface(
            &mut test_values.monitor_proxy,
            0,
            fidl_common::WlanMacRole::Client,
            NULL_MAC_ADDR,
            &test_values.telemetry_sender,
        );
        pin_mut!(fut);

        // Wait for the request to stall out waiting for DeviceMonitor.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Send back a failure from DeviceMonitor.
        send_create_iface_response(&mut exec, &mut test_values.monitor_stream, None);

        // The future should complete.
        assert_variant!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(Err(PhyManagerError::IfaceCreateFailure))
        );

        // Verify that a metric has been logged.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::IfaceCreationFailure))
        )
    }

    #[fuchsia::test]
    fn test_create_iface_request_fails() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        drop(test_values.monitor_stream);

        // Issue a create iface request
        let fut = create_iface(
            &mut test_values.monitor_proxy,
            0,
            fidl_common::WlanMacRole::Client,
            NULL_MAC_ADDR,
            &test_values.telemetry_sender,
        );
        pin_mut!(fut);

        // The request should immediately fail.
        assert_variant!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(Err(PhyManagerError::IfaceCreateFailure))
        );

        // Verify that a metric has been logged.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::IfaceCreationFailure))
        )
    }

    #[fuchsia::test]
    fn test_destroy_iface_succeeds() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Issue a destroy iface request
        let fut = destroy_iface(&mut test_values.monitor_proxy, 0, &test_values.telemetry_sender);
        pin_mut!(fut);

        // Wait for the request to stall out waiting for DeviceMonitor.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Send back a positive response from DeviceMonitor.
        send_destroy_iface_response(&mut exec, &mut test_values.monitor_stream, ZX_OK);

        // The future should complete.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));

        // Verify that there is nothing waiting on the telemetry receiver.
        assert_variant!(test_values.telemetry_receiver.try_next(), Err(_))
    }

    #[fuchsia::test]
    fn test_destroy_iface_not_found() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Issue a destroy iface request
        let fut = destroy_iface(&mut test_values.monitor_proxy, 0, &test_values.telemetry_sender);
        pin_mut!(fut);

        // Wait for the request to stall out waiting for DeviceMonitor.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Send back NOT_FOUND from DeviceMonitor.
        send_destroy_iface_response(&mut exec, &mut test_values.monitor_stream, ZX_ERR_NOT_FOUND);

        // The future should complete.
        assert_variant!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(Err(PhyManagerError::IfaceDestroyFailure))
        );

        // Verify that no metric has been logged.
        assert_variant!(test_values.telemetry_receiver.try_next(), Err(_))
    }

    #[fuchsia::test]
    fn test_destroy_iface_fails() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Issue a destroy iface request
        let fut = destroy_iface(&mut test_values.monitor_proxy, 0, &test_values.telemetry_sender);
        pin_mut!(fut);

        // Wait for the request to stall out waiting for DeviceMonitor.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Send back a non-NOT_FOUND failure from DeviceMonitor.
        send_destroy_iface_response(
            &mut exec,
            &mut test_values.monitor_stream,
            fuchsia_zircon::sys::ZX_ERR_NO_RESOURCES,
        );

        // The future should complete.
        assert_variant!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(Err(PhyManagerError::IfaceDestroyFailure))
        );

        // Verify that a metric has been logged.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::IfaceDestructionFailure))
        )
    }

    #[fuchsia::test]
    fn test_destroy_iface_request_fails() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        drop(test_values.monitor_stream);

        // Issue a destroy iface request
        let fut = destroy_iface(&mut test_values.monitor_proxy, 0, &test_values.telemetry_sender);
        pin_mut!(fut);

        // The request should immediately fail.
        assert_variant!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(Err(PhyManagerError::IfaceDestroyFailure))
        );

        // Verify that a metric has been logged.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::IfaceDestructionFailure))
        )
    }

    /// Verify that client iface failures are added properly.
    #[fuchsia::test]
    fn test_record_iface_event() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        let security_support = fidl_common::SecuritySupport {
            sae: fidl_common::SaeFeature {
                driver_handler_supported: false,
                sme_handler_supported: false,
            },
            mfp: fidl_common::MfpFeature { supported: false },
        };

        // Add some PHYs with interfaces.
        let _ = phy_manager.phys.insert(0, PhyContainer::new(vec![]));
        let _ = phy_manager.phys.insert(1, PhyContainer::new(vec![]));
        let _ = phy_manager.phys.insert(2, PhyContainer::new(vec![]));
        let _ = phy_manager.phys.insert(3, PhyContainer::new(vec![]));

        // Add some PHYs with interfaces.
        let _ = phy_manager
            .phys
            .get_mut(&0)
            .expect("missing PHY")
            .client_ifaces
            .insert(123, security_support.clone());
        let _ = phy_manager
            .phys
            .get_mut(&1)
            .expect("missing PHY")
            .client_ifaces
            .insert(456, security_support.clone());
        let _ = phy_manager
            .phys
            .get_mut(&2)
            .expect("missing PHY")
            .client_ifaces
            .insert(789, security_support);
        let _ = phy_manager.phys.get_mut(&3).expect("missing PHY").ap_ifaces.insert(246);

        // Allow defects to be retained indefinitely.
        phy_manager.phys.get_mut(&0).expect("missing PHY").defects = EventHistory::new(u32::MAX);
        phy_manager.phys.get_mut(&1).expect("missing PHY").defects = EventHistory::new(u32::MAX);
        phy_manager.phys.get_mut(&2).expect("missing PHY").defects = EventHistory::new(u32::MAX);
        phy_manager.phys.get_mut(&3).expect("missing PHY").defects = EventHistory::new(u32::MAX);

        // Log some client interface failures.
        {
            let defect_fut = phy_manager
                .record_defect(Defect::Iface(IfaceFailure::CanceledScan { iface_id: 123 }));
            pin_mut!(defect_fut);
            assert_variant!(exec.run_until_stalled(&mut defect_fut), Poll::Ready(()));
        }
        {
            let defect_fut = phy_manager
                .record_defect(Defect::Iface(IfaceFailure::FailedScan { iface_id: 456 }));
            pin_mut!(defect_fut);
            assert_variant!(exec.run_until_stalled(&mut defect_fut), Poll::Ready(()));
        }
        {
            let defect_fut = phy_manager
                .record_defect(Defect::Iface(IfaceFailure::EmptyScanResults { iface_id: 789 }));
            pin_mut!(defect_fut);
            assert_variant!(exec.run_until_stalled(&mut defect_fut), Poll::Ready(()));
        }
        {
            let defect_fut = phy_manager
                .record_defect(Defect::Iface(IfaceFailure::ConnectionFailure { iface_id: 123 }));
            pin_mut!(defect_fut);
            assert_variant!(exec.run_until_stalled(&mut defect_fut), Poll::Ready(()));
        }

        // Log an AP interface failure.
        {
            let defect_fut = phy_manager
                .record_defect(Defect::Iface(IfaceFailure::ApStartFailure { iface_id: 246 }));
            pin_mut!(defect_fut);
            assert_variant!(exec.run_until_stalled(&mut defect_fut), Poll::Ready(()));
        }

        // Verify that the defects have been logged.
        assert_eq!(phy_manager.phys[&0].defects.events.len(), 2);
        assert_eq!(
            phy_manager.phys[&0].defects.events[0].value,
            Defect::Iface(IfaceFailure::CanceledScan { iface_id: 123 })
        );
        assert_eq!(
            phy_manager.phys[&0].defects.events[1].value,
            Defect::Iface(IfaceFailure::ConnectionFailure { iface_id: 123 })
        );
        assert_eq!(phy_manager.phys[&1].defects.events.len(), 1);
        assert_eq!(
            phy_manager.phys[&1].defects.events[0].value,
            Defect::Iface(IfaceFailure::FailedScan { iface_id: 456 })
        );
        assert_eq!(phy_manager.phys[&2].defects.events.len(), 1);
        assert_eq!(
            phy_manager.phys[&2].defects.events[0].value,
            Defect::Iface(IfaceFailure::EmptyScanResults { iface_id: 789 })
        );
        assert_eq!(phy_manager.phys[&3].defects.events.len(), 1);
        assert_eq!(
            phy_manager.phys[&3].defects.events[0].value,
            Defect::Iface(IfaceFailure::ApStartFailure { iface_id: 246 })
        );
    }

    /// Verify that AP ifaces do not receive client failures..
    #[fuchsia::test]
    fn test_aps_do_not_record_client_defects() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Add some PHYs with interfaces.
        let _ = phy_manager.phys.insert(0, PhyContainer::new(vec![]));

        // Add some PHYs with interfaces.
        let _ = phy_manager.phys.get_mut(&0).expect("missing PHY").ap_ifaces.insert(123);

        // Allow defects to be retained indefinitely.
        phy_manager.phys.get_mut(&0).expect("missing PHY").defects = EventHistory::new(u32::MAX);

        // Log some client interface failures.
        {
            let defect_fut = phy_manager
                .record_defect(Defect::Iface(IfaceFailure::CanceledScan { iface_id: 123 }));
            pin_mut!(defect_fut);
            assert_variant!(exec.run_until_stalled(&mut defect_fut), Poll::Ready(()));
        }
        {
            let defect_fut = phy_manager
                .record_defect(Defect::Iface(IfaceFailure::FailedScan { iface_id: 123 }));
            pin_mut!(defect_fut);
            assert_variant!(exec.run_until_stalled(&mut defect_fut), Poll::Ready(()));
        }
        {
            let defect_fut = phy_manager
                .record_defect(Defect::Iface(IfaceFailure::EmptyScanResults { iface_id: 123 }));
            pin_mut!(defect_fut);
            assert_variant!(exec.run_until_stalled(&mut defect_fut), Poll::Ready(()));
        }
        {
            let defect_fut = phy_manager
                .record_defect(Defect::Iface(IfaceFailure::ConnectionFailure { iface_id: 123 }));
            pin_mut!(defect_fut);
            assert_variant!(exec.run_until_stalled(&mut defect_fut), Poll::Ready(()));
        }

        // Verify that the defects have been logged.
        assert_eq!(phy_manager.phys[&0].defects.events.len(), 0);
    }

    /// Verify that client ifaces do not receive AP defects.
    #[fuchsia::test]
    fn test_clients_do_not_record_ap_defects() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let mut phy_manager = PhyManager::new(
            test_values.monitor_proxy,
            test_values.node,
            test_values.telemetry_sender,
        );

        // Add some PHYs with interfaces.
        let _ = phy_manager.phys.insert(0, PhyContainer::new(vec![]));

        // Add a PHY with a client interface.
        let security_support = fidl_common::SecuritySupport {
            sae: fidl_common::SaeFeature {
                driver_handler_supported: false,
                sme_handler_supported: false,
            },
            mfp: fidl_common::MfpFeature { supported: false },
        };
        let _ = phy_manager
            .phys
            .get_mut(&0)
            .expect("missing PHY")
            .client_ifaces
            .insert(123, security_support);

        // Allow defects to be retained indefinitely.
        phy_manager.phys.get_mut(&0).expect("missing PHY").defects = EventHistory::new(u32::MAX);

        // Log an AP interface failure.
        {
            let defect_fut = phy_manager
                .record_defect(Defect::Iface(IfaceFailure::ApStartFailure { iface_id: 123 }));
            pin_mut!(defect_fut);
            assert_variant!(exec.run_until_stalled(&mut defect_fut), Poll::Ready(()));
        }

        // Verify that the defects have been not logged.
        assert_eq!(phy_manager.phys[&0].defects.events.len(), 0);
    }
}
