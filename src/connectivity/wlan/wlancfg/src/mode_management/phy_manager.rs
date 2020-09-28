// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    eui48::MacAddress,
    fidl_fuchsia_wlan_device as fidl_device,
    fidl_fuchsia_wlan_device::MacRole,
    fidl_fuchsia_wlan_device_service as fidl_service, fuchsia_zircon,
    log::{info, warn},
    std::collections::{HashMap, HashSet},
    thiserror::Error,
};

/// Errors raised while attempting to query information about or configure PHYs and ifaces.
#[derive(Debug, Error)]
pub(crate) enum PhyManagerError {
    #[error("the requested operation is not supported")]
    Unsupported,
    #[error("unable to query phy information")]
    PhyQueryFailure,
    #[error("unable to query iface information")]
    IfaceQueryFailure,
    #[error("unable to create iface")]
    IfaceCreateFailure,
    #[error("unable to destroy iface")]
    IfaceDestroyFailure,
}

/// Stores information about a WLAN PHY and any interfaces that belong to it.
pub(crate) struct PhyContainer {
    phy_info: fidl_device::PhyInfo,
    client_ifaces: HashSet<u16>,
    ap_ifaces: HashSet<u16>,
}

#[async_trait]
pub(crate) trait PhyManagerApi {
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
    /// discovered PHYs, create client interfaces if the PHY can support them.
    async fn create_all_client_ifaces(&mut self) -> Result<(), PhyManagerError>;

    /// Destroys all client interfaces.  Do not allow the creation of client interfaces for newly
    /// discovered PHYs.
    async fn destroy_all_client_ifaces(&mut self) -> Result<(), PhyManagerError>;

    /// Finds a PHY with a client interface and returns the interface's ID to the caller.
    fn get_client(&mut self) -> Option<u16>;

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
}

/// Maintains a record of all PHYs that are present and their associated interfaces.
pub(crate) struct PhyManager {
    phys: HashMap<u16, PhyContainer>,
    device_service: fidl_service::DeviceServiceProxy,
    client_connections_enabled: bool,
    suggested_ap_mac: Option<MacAddress>,
}

impl PhyContainer {
    /// Stores the PhyInfo associated with a newly discovered PHY and creates empty vectors to hold
    /// interface IDs that belong to this PHY.
    pub fn new(phy_info: fidl_device::PhyInfo) -> Self {
        PhyContainer {
            phy_info: phy_info,
            client_ifaces: HashSet::new(),
            ap_ifaces: HashSet::new(),
        }
    }
}

// TODO(fxbug.dev/49590): PhyManager makes the assumption that WLAN PHYs that support client and AP modes can
// can operate as clients and APs simultaneously.  For PHYs where this is not the case, the
// existing interface should be destroyed before the new interface is created.
impl PhyManager {
    /// Internally stores a DeviceServiceProxy to query PHY and interface properties and create and
    /// destroy interfaces as requested.
    pub fn new(device_service: fidl_service::DeviceServiceProxy) -> Self {
        PhyManager {
            phys: HashMap::new(),
            device_service,
            client_connections_enabled: false,
            suggested_ap_mac: None,
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
            .filter_map(|(k, v)| if v.phy_info.mac_roles.contains(&role) { Some(*k) } else { None })
            .collect()
    }
}

#[async_trait]
impl PhyManagerApi for PhyManager {
    async fn add_phy(&mut self, phy_id: u16) -> Result<(), PhyManagerError> {
        let query_phy_response = match self
            .device_service
            .query_phy(&mut fidl_service::QueryPhyRequest { phy_id: phy_id })
            .await
        {
            Ok((status, query_phy_response)) => {
                if fuchsia_zircon::ok(status).is_err() {
                    return Err(PhyManagerError::PhyQueryFailure);
                }
                query_phy_response
            }
            Err(_) => {
                return Err(PhyManagerError::PhyQueryFailure);
            }
        };

        if let Some(response) = query_phy_response {
            info!("adding PHY ID #{}", phy_id);
            let phy_id = response.info.id;
            let mut phy_container = PhyContainer::new(response.info);

            if self.client_connections_enabled
                && phy_container.phy_info.mac_roles.contains(&MacRole::Client)
            {
                let iface_id =
                    create_iface(&self.device_service, phy_id, MacRole::Client, None).await?;
                phy_container.client_ifaces.insert(iface_id);
            }

            self.phys.insert(phy_id, phy_container);
        }

        Ok(())
    }

    fn remove_phy(&mut self, phy_id: u16) {
        self.phys.remove(&phy_id);
    }

    async fn on_iface_added(&mut self, iface_id: u16) -> Result<(), PhyManagerError> {
        if let Some(query_iface_response) = self.query_iface(iface_id).await? {
            let phy = self.ensure_phy(query_iface_response.phy_id).await?;
            let iface_id = query_iface_response.id;

            match query_iface_response.role {
                MacRole::Client => {
                    if !phy.client_ifaces.contains(&iface_id) {
                        warn!("Detected an unexpected client iface created outside of PhyManager");
                        let _ = phy.client_ifaces.insert(iface_id);
                    }
                }
                MacRole::Ap => {
                    if !phy.ap_ifaces.contains(&iface_id) {
                        warn!("Detected an unexpected AP iface created outside of PhyManager");
                        let _ = phy.ap_ifaces.insert(iface_id);
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
            phy_info.client_ifaces.remove(&iface_id);
            phy_info.ap_ifaces.remove(&iface_id);
        }
    }

    async fn create_all_client_ifaces(&mut self) -> Result<(), PhyManagerError> {
        self.client_connections_enabled = true;

        let client_capable_phy_ids = self.phys_for_role(MacRole::Client);

        for client_phy in client_capable_phy_ids.iter() {
            let phy_container =
                self.phys.get_mut(&client_phy).ok_or(PhyManagerError::PhyQueryFailure)?;
            if phy_container.client_ifaces.is_empty() {
                let iface_id =
                    create_iface(&self.device_service, *client_phy, MacRole::Client, None).await?;
                phy_container.client_ifaces.insert(iface_id);
            }
        }

        Ok(())
    }

    async fn destroy_all_client_ifaces(&mut self) -> Result<(), PhyManagerError> {
        self.client_connections_enabled = false;

        let client_capable_phys = self.phys_for_role(MacRole::Client);
        let mut result = Ok(());

        for client_phy in client_capable_phys.iter() {
            let phy_container =
                self.phys.get_mut(&client_phy).ok_or(PhyManagerError::PhyQueryFailure)?;

            // Continue tracking interface IDs for which deletion fails.
            let mut lingering_ifaces = HashSet::new();

            for iface_id in phy_container.client_ifaces.drain() {
                match destroy_iface(&self.device_service, iface_id).await {
                    Ok(()) => {}
                    Err(e) => {
                        result = Err(e);
                        lingering_ifaces.insert(iface_id);
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
        match phy.client_ifaces.iter().next() {
            Some(iface_id) => Some(*iface_id),
            None => None,
        }
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
                    create_iface(&self.device_service, *ap_phy_id, MacRole::Ap, mac).await?;

                phy_container.ap_ifaces.insert(iface_id);
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
        let iface_info = match self.query_iface(iface_id).await? {
            Some(iface_info) => iface_info,
            None => return Err(PhyManagerError::IfaceQueryFailure),
        };
        let phy_container = match self.phys.get_mut(&iface_info.phy_id) {
            Some(phy_container) => phy_container,
            None => return Err(PhyManagerError::IfaceQueryFailure),
        };

        if phy_container.ap_ifaces.remove(&iface_id) {
            match destroy_iface(&self.device_service, iface_id).await {
                Ok(()) => {}
                Err(e) => {
                    phy_container.ap_ifaces.insert(iface_id);
                    return Err(e);
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
                match destroy_iface(&self.device_service, iface_id).await {
                    Ok(()) => {}
                    Err(e) => {
                        result = Err(e);
                        lingering_ifaces.insert(iface_id);
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
}

/// Creates an interface of the requested role for the requested PHY ID.  Returns either the
/// ID of the created interface or an error.
async fn create_iface(
    proxy: &fidl_service::DeviceServiceProxy,
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
    proxy: &fidl_service::DeviceServiceProxy,
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints,
        fidl_fuchsia_wlan_device as fidl_device, fidl_fuchsia_wlan_device_service as fidl_service,
        fuchsia_async::{run_singlethreaded, Executor},
        fuchsia_zircon::sys::{ZX_ERR_NOT_FOUND, ZX_OK},
        futures::stream::StreamExt,
        futures::task::Poll,
        pin_utils::pin_mut,
        wlan_common::assert_variant,
    };

    /// Hold the client and service ends for DeviceService to allow mocking DeviceService responses
    /// for unit tests.
    struct TestValues {
        proxy: fidl_service::DeviceServiceProxy,
        stream: fidl_service::DeviceServiceRequestStream,
    }

    /// Create a TestValues for a unit test.
    fn test_setup() -> TestValues {
        let (proxy, requests) = endpoints::create_proxy::<fidl_service::DeviceServiceMarker>()
            .expect("failed to create SeviceService proxy");
        let stream = requests.into_stream().expect("failed to create stream");

        TestValues { proxy: proxy, stream: stream }
    }

    /// Take in the service side of a DeviceService::QueryPhy request and respond with the given
    /// PhyInfo.
    fn send_query_phy_response(
        exec: &mut Executor,
        server: &mut fidl_service::DeviceServiceRequestStream,
        phy_info: Option<fidl_device::PhyInfo>,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut server.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceServiceRequest::QueryPhy {
                    responder, ..
                }
            ))) => {
                match phy_info {
                    Some(phy_info) => responder.send(
                        ZX_OK,
                        Some(&mut fidl_service::QueryPhyResponse {
                            info: phy_info,
                        })
                    )
                    .expect("sending fake phy info"),
                    None => responder.send(ZX_ERR_NOT_FOUND, None).expect("sending fake response with none")
                }
            }
        );
    }

    /// Create a PhyInfo object for unit testing.
    fn send_query_iface_response(
        exec: &mut Executor,
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

    /// Handles the service side of a DeviceService::CreateIface request by replying with the
    /// provided optional iface ID.
    fn send_create_iface_response(
        exec: &mut Executor,
        server: &mut fidl_service::DeviceServiceRequestStream,
        iface_id: Option<u16>,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut server.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceServiceRequest::CreateIface {
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

    /// Handles the service side of a DeviceService::DestroyIface request by replying with the
    /// provided zx_status_t.
    fn send_destroy_iface_response(
        exec: &mut Executor,
        server: &mut fidl_service::DeviceServiceRequestStream,
        return_status: fuchsia_zircon::zx_status_t,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut server.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceServiceRequest::DestroyIface {
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
        fidl_device::PhyInfo {
            id: id,
            dev_path: None,
            hw_mac_address: [0, 1, 2, 3, 4, 5],
            supported_phys: Vec::new(),
            driver_features: Vec::new(),
            mac_roles: mac_roles,
            caps: Vec::new(),
            bands: Vec::new(),
        }
    }

    /// Creates a QueryIfaceResponse from the arguments provided by the caller.
    fn create_iface_response(
        role: MacRole,
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
            mac_addr: mac,
        }
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnPhyCreated event and
    /// calling add_phy on PhyManager for a PHY that exists.  The expectation is that the
    /// PhyManager initially does not have any PHYs available.  After the call to add_phy, the
    /// PhyManager should have a new PhyContainer.
    #[test]
    fn add_valid_phy() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles.clone());

        let mut phy_manager = PhyManager::new(test_values.proxy);
        {
            let add_phy_fut = phy_manager.add_phy(phy_info.id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_query_phy_response(&mut exec, &mut test_values.stream, Some(phy_info));

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));
        assert_eq!(
            phy_manager.phys.get(&fake_phy_id).unwrap().phy_info,
            fake_phy_info(fake_phy_id, fake_mac_roles)
        );
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnPhyCreated event and
    /// calling add_phy on PhyManager for a PHY that does not exist.  The PhyManager in this case
    /// should not create and store a new PhyContainer.
    #[test]
    fn add_invalid_phy() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        {
            let add_phy_fut = phy_manager.add_phy(1);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_query_phy_response(&mut exec, &mut test_values.stream, None);

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }
        assert!(phy_manager.phys.is_empty());
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnPhyCreated event and
    /// calling add_phy on PhyManager for a PHY that has already been accounted for, but whose
    /// properties have changed.  The PhyManager in this case should update the associated PhyInfo.
    #[test]
    fn add_duplicate_phy() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles.clone());

        {
            let add_phy_fut = phy_manager.add_phy(phy_info.id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_query_phy_response(&mut exec, &mut test_values.stream, Some(phy_info));

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        {
            assert!(phy_manager.phys.contains_key(&fake_phy_id));
            assert_eq!(
                phy_manager.phys.get(&fake_phy_id).unwrap().phy_info,
                fake_phy_info(fake_phy_id, fake_mac_roles.clone())
            );
        }

        // Send an update for the same PHY ID and ensure that the PHY info is updated.
        let mut phy_info = fake_phy_info(fake_phy_id, fake_mac_roles.clone());
        phy_info.hw_mac_address = [5, 4, 3, 2, 1, 0];

        {
            let add_phy_fut = phy_manager.add_phy(phy_info.id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_query_phy_response(&mut exec, &mut test_values.stream, Some(phy_info));

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        let mut phy_info = fake_phy_info(fake_phy_id, fake_mac_roles.clone());
        phy_info.hw_mac_address = [5, 4, 3, 2, 1, 0];

        assert!(phy_manager.phys.contains_key(&fake_phy_id));
        assert_eq!(phy_manager.phys.get(&fake_phy_id).unwrap().phy_info, phy_info);
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnPhyRemoved event and
    /// calling remove_phy on PhyManager for a PHY that not longer exists.  The PhyManager in this
    /// case should remove the PhyContainer associated with the removed PHY ID.
    #[test]
    fn add_phy_after_create_all_client_ifaces() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Client];
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles.clone());

        {
            let start_connections_fut = phy_manager.create_all_client_ifaces();
            pin_mut!(start_connections_fut);
            assert!(exec.run_until_stalled(&mut start_connections_fut).is_ready());
        }

        // Add a new phy.  Since client connections have been started, it should also create a
        // client iface.
        {
            let add_phy_fut = phy_manager.add_phy(phy_info.id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_query_phy_response(&mut exec, &mut test_values.stream, Some(phy_info));

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());

            send_create_iface_response(&mut exec, &mut test_values.stream, Some(fake_iface_id));

            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));
        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.client_ifaces.contains(&fake_iface_id));
    }

    #[run_singlethreaded(test)]
    async fn remove_valid_phy() {
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);

        let phy_container = PhyContainer::new(phy_info);
        phy_manager.phys.insert(fake_phy_id, phy_container);
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
        let mut phy_manager = PhyManager::new(test_values.proxy);

        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);

        let phy_container = PhyContainer::new(phy_info);
        phy_manager.phys.insert(fake_phy_id, phy_container);
        phy_manager.remove_phy(2);
        assert!(phy_manager.phys.contains_key(&fake_phy_id));
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnIfaceAdded event for
    /// an iface that belongs to a PHY that has been accounted for.  The PhyManager should add the
    /// newly discovered iface to the existing PHY's list of client ifaces.
    #[test]
    fn on_iface_added() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);

        let phy_container = PhyContainer::new(phy_info);

        // Create an IfaceResponse to be sent to the PhyManager when the iface ID is queried
        let fake_role = MacRole::Client;
        let fake_iface_id = 1;
        let fake_phy_assigned_id = 1;
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let mut iface_response = create_iface_response(
            fake_role,
            fake_iface_id,
            fake_phy_id,
            fake_phy_assigned_id,
            fake_mac_addr,
        );

        {
            // Inject the fake PHY information
            phy_manager.phys.insert(fake_phy_id, phy_container);

            // Add the fake iface
            let on_iface_added_fut = phy_manager.on_iface_added(fake_iface_id);
            pin_mut!(on_iface_added_fut);
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_query_iface_response(
                &mut exec,
                &mut test_values.stream,
                Some(&mut iface_response),
            );

            // Wait for the PhyManager to finish processing the received iface information
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_ready());
        }

        // Expect that the PhyContainer associated with the fake PHY has been updated with the
        // fake client
        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.client_ifaces.contains(&fake_iface_id));
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnIfaceAdded event for
    /// an iface that belongs to a PHY that has not been accounted for.  The PhyManager should
    /// query the PHY's information, create a new PhyContainer, and insert the new iface ID into
    /// the PHY's list of client ifaces.
    #[test]
    fn on_iface_added_missing_phy() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);

        // Create an IfaceResponse to be sent to the PhyManager when the iface ID is queried
        let fake_role = MacRole::Client;
        let fake_iface_id = 1;
        let fake_phy_assigned_id = 1;
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let mut iface_response = create_iface_response(
            fake_role,
            fake_iface_id,
            fake_phy_id,
            fake_phy_assigned_id,
            fake_mac_addr,
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
                &mut test_values.stream,
                Some(&mut iface_response),
            );

            // And then the PHY information is queried.
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_query_phy_response(&mut exec, &mut test_values.stream, Some(phy_info));

            // Wait for the PhyManager to finish processing the received iface information
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_ready());
        }

        // Expect that the PhyContainer associated with the fake PHY has been updated with the
        // fake client
        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.client_ifaces.contains(&fake_iface_id));
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnIfaceAdded event for
    /// an iface that was created by PhyManager and has already been accounted for.  The PhyManager
    /// should simply ignore the duplicate iface ID and not append it to its list of clients.
    #[test]
    fn add_duplicate_iface() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);

        // Inject the fake PHY information
        let phy_container = PhyContainer::new(phy_info);
        phy_manager.phys.insert(fake_phy_id, phy_container);

        // Create an IfaceResponse to be sent to the PhyManager when the iface ID is queried
        let fake_role = MacRole::Client;
        let fake_iface_id = 1;
        let fake_phy_assigned_id = 1;
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let mut iface_response = create_iface_response(
            fake_role,
            fake_iface_id,
            fake_phy_id,
            fake_phy_assigned_id,
            fake_mac_addr,
        );

        // Add the same iface ID twice
        for _ in 0..2 {
            // Add the fake iface
            let on_iface_added_fut = phy_manager.on_iface_added(fake_iface_id);
            pin_mut!(on_iface_added_fut);
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_query_iface_response(
                &mut exec,
                &mut test_values.stream,
                Some(&mut iface_response),
            );

            // Wait for the PhyManager to finish processing the received iface information
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_ready());
        }

        // Expect that the PhyContainer associated with the fake PHY has been updated with only one
        // reference to the fake client
        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert_eq!(phy_container.client_ifaces.len(), 1);
        assert!(phy_container.client_ifaces.contains(&fake_iface_id));
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnIfaceAdded event for
    /// an iface that has already been removed.  The PhyManager should fail to query the iface info
    /// and not account for the iface ID.
    #[test]
    fn add_nonexistent_iface() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        {
            // Add the non-existent iface
            let on_iface_added_fut = phy_manager.on_iface_added(1);
            pin_mut!(on_iface_added_fut);
            assert!(exec.run_until_stalled(&mut on_iface_added_fut).is_pending());

            send_query_iface_response(&mut exec, &mut test_values.stream, None);

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
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);

        // Inject the fake PHY information
        let mut phy_container = PhyContainer::new(phy_info);
        let fake_iface_id = 1;
        phy_container.client_ifaces.insert(fake_iface_id);

        phy_manager.phys.insert(fake_phy_id, phy_container);

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
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = Vec::new();
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);

        let present_iface_id = 1;
        let removed_iface_id = 2;

        // Inject the fake PHY information
        let mut phy_container = PhyContainer::new(phy_info);
        phy_container.client_ifaces.insert(present_iface_id);
        phy_container.client_ifaces.insert(removed_iface_id);
        phy_manager.phys.insert(fake_phy_id, phy_container);
        phy_manager.on_iface_removed(removed_iface_id);

        // Expect that the iface ID has been removed from the PhyContainer
        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert_eq!(phy_container.client_ifaces.len(), 1);
        assert!(phy_container.client_ifaces.contains(&present_iface_id));
    }

    /// Tests the response of the PhyManager when a client iface is requested, but no PHYs are
    /// present.  The expectation is that the PhyManager returns None.
    #[run_singlethreaded(test)]
    async fn get_client_no_phys() {
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        let client = phy_manager.get_client();
        assert!(client.is_none());
    }

    /// Tests the response of the PhyManager when a client iface is requested, a client-capable PHY
    /// has been discovered, but client connections have not been started.  The expectation is that
    /// the PhyManager returns None.
    #[run_singlethreaded(test)]
    async fn get_unconfigured_client() {
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Client];
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
        let phy_container = PhyContainer::new(phy_info);

        phy_manager.phys.insert(fake_phy_id, phy_container);

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
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Client];
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
        let phy_container = PhyContainer::new(phy_info);

        phy_manager.phys.insert(fake_phy_id, phy_container);

        // Insert the fake iface
        let fake_iface_id = 1;
        let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
        phy_container.client_ifaces.insert(fake_iface_id);

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
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Ap];
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
        let mut phy_container = PhyContainer::new(phy_info);
        phy_container.ap_ifaces.insert(fake_iface_id);

        phy_manager.phys.insert(fake_phy_id, phy_container);

        // Retrieve the client ID
        let client = phy_manager.get_client();
        assert!(client.is_none());
    }

    /// Tests the PhyManager's response to stop_client_connection when there is an existing client
    /// iface.  The expectation is that the client iface is destroyed and there is no remaining
    /// record of the iface ID in the PhyManager.
    #[test]
    fn destroy_all_client_ifaces() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Client];
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
        let phy_container = PhyContainer::new(phy_info);

        {
            phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            phy_container.client_ifaces.insert(fake_iface_id);

            // Stop client connections
            let stop_clients_future = phy_manager.destroy_all_client_ifaces();
            pin_mut!(stop_clients_future);

            assert!(exec.run_until_stalled(&mut stop_clients_future).is_pending());

            send_destroy_iface_response(&mut exec, &mut test_values.stream, ZX_OK);

            assert!(exec.run_until_stalled(&mut stop_clients_future).is_ready());
        }

        // Ensure that the client interface that was added has been removed.
        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(!phy_container.client_ifaces.contains(&fake_iface_id));
    }

    /// Tests the PhyManager's response to destroy_all_client_ifaces when no client ifaces are
    /// present but an AP iface is present.  The expectation is that the AP iface is left intact.
    #[test]
    fn destroy_all_client_ifaces_no_clients() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![MacRole::Ap];
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
        let phy_container = PhyContainer::new(phy_info);

        // Insert the fake AP iface and then stop clients
        {
            phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake AP iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            phy_container.ap_ifaces.insert(fake_iface_id);

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
    #[test]
    fn get_ap_no_phys() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        let get_ap_future = phy_manager.create_or_get_ap_iface();

        pin_mut!(get_ap_future);
        assert_variant!(exec.run_until_stalled(&mut get_ap_future), Poll::Ready(Ok(None)));
    }

    /// Tests the PhyManager's response when the PhyManager holds a PHY that can have an AP iface
    /// but the AP iface has not been created yet.  The expectation is that the PhyManager creates
    /// a new AP iface and returns its ID to the caller.
    #[test]
    fn get_unconfigured_ap() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Ap];
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
        let phy_container = PhyContainer::new(phy_info);

        phy_manager.phys.insert(fake_phy_id, phy_container);

        // Retrieve the AP interface ID
        let fake_iface_id = 1;
        {
            let get_ap_future = phy_manager.create_or_get_ap_iface();

            pin_mut!(get_ap_future);
            assert!(exec.run_until_stalled(&mut get_ap_future).is_pending());

            send_create_iface_response(&mut exec, &mut test_values.stream, Some(fake_iface_id));
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
    #[test]
    fn get_configured_ap() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Ap];
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
        let phy_container = PhyContainer::new(phy_info);

        phy_manager.phys.insert(fake_phy_id, phy_container);

        // Insert the fake iface
        let fake_iface_id = 1;
        let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
        phy_container.ap_ifaces.insert(fake_iface_id);

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
    #[test]
    fn get_ap_no_compatible_phys() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Client];
        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
        let phy_container = PhyContainer::new(phy_info);

        phy_manager.phys.insert(fake_phy_id, phy_container);

        // Retrieve the client ID
        let get_ap_future = phy_manager.create_or_get_ap_iface();
        pin_mut!(get_ap_future);
        assert_variant!(exec.run_until_stalled(&mut get_ap_future), Poll::Ready(Ok(None)));
    }

    /// This test stops a valid AP iface on a PhyManager.  The expectation is that the PhyManager
    /// should retain the record of the PHY, but the AP iface ID should be removed.
    #[test]
    fn stop_valid_ap_iface() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Ap];

        {
            let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
            let phy_container = PhyContainer::new(phy_info);

            phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            phy_container.ap_ifaces.insert(fake_iface_id);

            // Remove the AP iface ID
            let destroy_ap_iface_future = phy_manager.destroy_ap_iface(fake_iface_id);
            pin_mut!(destroy_ap_iface_future);
            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_pending());

            // Send back a query interface respone
            let mut iface_response =
                create_iface_response(MacRole::Ap, 1, 1, 1, [0, 1, 2, 3, 4, 5]);
            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_pending());
            send_query_iface_response(
                &mut exec,
                &mut test_values.stream,
                Some(&mut iface_response),
            );

            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_pending());
            send_destroy_iface_response(&mut exec, &mut test_values.stream, ZX_OK);

            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(!phy_container.ap_ifaces.contains(&fake_iface_id));
    }

    /// This test attempts to stop an invalid AP iface ID.  The expectation is that a valid iface
    /// ID is unaffected.
    #[test]
    fn stop_invalid_ap_iface() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Ap];

        {
            let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
            let phy_container = PhyContainer::new(phy_info);

            phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            phy_container.ap_ifaces.insert(fake_iface_id);

            // Remove a non-existent AP iface ID
            let destroy_ap_iface_future = phy_manager.destroy_ap_iface(2);
            pin_mut!(destroy_ap_iface_future);
            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_pending());

            // Send back an empty response
            send_query_iface_response(&mut exec, &mut test_values.stream, None);
            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.ap_ifaces.contains(&fake_iface_id));
    }

    /// This test creates two AP ifaces for a PHY that supports AP ifaces.  destroy_all_ap_ifaces is then
    /// called on the PhyManager.  The expectation is that both AP ifaces should be destroyed and
    /// the records of the iface IDs should be removed from the PhyContainer.
    #[test]
    fn stop_all_ap_ifaces() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // ifaces are added.
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Ap];

        {
            let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
            let phy_container = PhyContainer::new(phy_info);

            phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            phy_container.ap_ifaces.insert(0);
            phy_container.ap_ifaces.insert(1);

            // Expect two interface destruction requests
            let destroy_ap_iface_future = phy_manager.destroy_all_ap_ifaces();
            pin_mut!(destroy_ap_iface_future);

            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_pending());
            send_destroy_iface_response(&mut exec, &mut test_values.stream, ZX_OK);

            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_pending());
            send_destroy_iface_response(&mut exec, &mut test_values.stream, ZX_OK);

            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.ap_ifaces.is_empty());
    }

    /// This test calls destroy_all_ap_ifaces on a PhyManager that only has a client iface.  The expectation
    /// is that no interfaces should be destroyed and the client iface ID should remain in the
    /// PhyManager
    #[test]
    fn stop_all_ap_ifaces_with_client() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Client];

        {
            let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
            let phy_container = PhyContainer::new(phy_info);

            phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            phy_container.client_ifaces.insert(fake_iface_id);

            // Stop all AP ifaces
            let destroy_ap_iface_future = phy_manager.destroy_all_ap_ifaces();
            pin_mut!(destroy_ap_iface_future);
            assert!(exec.run_until_stalled(&mut destroy_ap_iface_future).is_ready());
        }

        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.client_ifaces.contains(&fake_iface_id));
    }

    /// Verifies that setting a suggested AP MAC address results in that MAC address being used as
    /// a part of the request to create an AP interface.  Ensures that this does not affect client
    /// interface requests.
    #[test]
    fn test_suggest_ap_mac() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Ap];

        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
        let phy_container = PhyContainer::new(phy_info);

        phy_manager.phys.insert(fake_phy_id, phy_container);

        // Insert the fake iface
        let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
        phy_container.client_ifaces.insert(fake_iface_id);

        // Suggest an AP MAC
        let mac = MacAddress::from_bytes(&[1, 2, 3, 4, 5, 6]).unwrap();
        phy_manager.suggest_ap_mac(mac.clone());

        let get_ap_future = phy_manager.create_or_get_ap_iface();
        pin_mut!(get_ap_future);
        assert_variant!(exec.run_until_stalled(&mut get_ap_future), Poll::Pending);

        // Verify that the suggested MAC is included in the request
        assert_variant!(
            exec.run_until_stalled(&mut test_values.stream.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceServiceRequest::CreateIface {
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

    #[test]
    fn test_suggested_mac_does_not_apply_to_client() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_mac_roles = vec![fidl_fuchsia_wlan_device::MacRole::Client];

        let phy_info = fake_phy_info(fake_phy_id, fake_mac_roles);
        let phy_container = PhyContainer::new(phy_info);

        phy_manager.phys.insert(fake_phy_id, phy_container);

        // Suggest an AP MAC
        let mac = MacAddress::from_bytes(&[1, 2, 3, 4, 5, 6]);
        phy_manager.suggest_ap_mac(mac.clone().unwrap());

        // Start client connections so that an IfaceRequest is issued for the client.
        let start_client_future = phy_manager.create_all_client_ifaces();
        pin_mut!(start_client_future);
        assert_variant!(exec.run_until_stalled(&mut start_client_future), Poll::Pending);

        // Verify that the suggested MAC is NOT included in the request
        assert_variant!(
            exec.run_until_stalled(&mut test_values.stream.next()),
            Poll::Ready(Some(Ok(
                fidl_service::DeviceServiceRequest::CreateIface {
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
        assert_variant!(exec.run_until_stalled(&mut start_client_future), Poll::Ready(_));
    }

    /// Tests get_phy_ids() when no PHYs are present. The expectation is that the PhyManager will
    /// return an empty `Vec` in this case.
    #[run_singlethreaded(test)]
    async fn get_phy_ids_no_phys() {
        let test_values = test_setup();
        let phy_manager = PhyManager::new(test_values.proxy);
        assert_eq!(phy_manager.get_phy_ids(), Vec::<u16>::new());
    }

    /// Tests get_phy_ids() when a single PHY is present. The expectation is that the PhyManager will
    /// return a single element `Vec`, with the appropriate ID.
    #[test]
    fn get_phy_ids_single_phy() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        {
            let phy_info = fake_phy_info(1, vec![]);
            let add_phy_fut = phy_manager.add_phy(phy_info.id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());
            send_query_phy_response(&mut exec, &mut test_values.stream, Some(phy_info));
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        assert_eq!(phy_manager.get_phy_ids(), vec![1]);
    }

    /// Tests get_phy_ids() when two PHYs are present. The expectation is that the PhyManager will
    /// return a two-element `Vec`, containing the appropriate IDs. Ordering is not guaranteed.
    #[test]
    fn get_phy_ids_two_phys() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        {
            let phy_info = fake_phy_info(1, Vec::new());
            let add_phy_fut = phy_manager.add_phy(phy_info.id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());
            send_query_phy_response(&mut exec, &mut test_values.stream, Some(phy_info));
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        {
            let phy_info = fake_phy_info(2, Vec::new());
            let add_phy_fut = phy_manager.add_phy(phy_info.id);
            pin_mut!(add_phy_fut);
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_pending());
            send_query_phy_response(&mut exec, &mut test_values.stream, Some(phy_info));
            assert!(exec.run_until_stalled(&mut add_phy_fut).is_ready());
        }

        let phy_ids = phy_manager.get_phy_ids();
        assert!(phy_ids.contains(&1), "expected phy_ids to contain `1`, but phy_ids={:?}", phy_ids);
        assert!(phy_ids.contains(&2), "expected phy_ids to contain `2`, but phy_ids={:?}", phy_ids);
    }
}
