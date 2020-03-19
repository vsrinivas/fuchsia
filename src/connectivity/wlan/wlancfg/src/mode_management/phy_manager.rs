// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use {
    fidl_fuchsia_wlan_device::{self as fidl_device, MacRole},
    fidl_fuchsia_wlan_device_service as fidl_service, fuchsia_zircon,
    log::{info, warn},
    std::collections::{HashMap, HashSet},
    thiserror::Error,
};

/// Errors raised while attempting to query information about or configure PHYs and ifaces.
#[derive(Debug, Error)]
enum PhyManagerError {
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
struct PhyContainer {
    phy_info: fidl_device::PhyInfo,
    client_ifaces: HashSet<u16>,
    ap_ifaces: HashSet<u16>,
}

/// Maintains a record of all PHYs that are present and their associated interfaces.
struct PhyManager {
    phys: HashMap<u16, PhyContainer>,
    device_service: fidl_service::DeviceServiceProxy,
    client_connections_enabled: bool,
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

impl PhyManager {
    /// Internally stores a DeviceServiceProxy to query PHY and interface properties and create and
    /// destroy interfaces as requested.
    pub fn new(device_service: fidl_service::DeviceServiceProxy) -> Self {
        PhyManager { phys: HashMap::new(), device_service, client_connections_enabled: false }
    }

    /// Checks to see if this PHY is already accounted for.  If it is not, queries its PHY
    /// attributes and places it in the hash map.
    pub async fn add_phy(&mut self, phy_id: u16) -> Result<(), PhyManagerError> {
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
                let iface_id = create_iface(&self.device_service, phy_id, MacRole::Client).await?;
                phy_container.client_ifaces.insert(iface_id);
            }

            self.phys.insert(phy_id, phy_container);
        }

        Ok(())
    }

    /// If the PHY is accounted for, removes the associated `PhyContainer` from the hash map.
    pub fn remove_phy(&mut self, phy_id: u16) {
        self.phys.remove(&phy_id);
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

    /// Queries the interface properties to get the PHY ID.  If the `PhyContainer`
    /// representing the interface's parent PHY is already present and its
    /// interface information is obsolete, updates it.  The PhyManager will track ifaces
    /// as it creates and deletes them, but it is possible that another caller circumvents the
    /// policy layer and creates an interface.  If no `PhyContainer` exists
    /// for the new interface, creates one and adds the newly discovered interface
    /// ID to it.
    pub async fn on_iface_added(&mut self, iface_id: u16) -> Result<(), PhyManagerError> {
        if let Some(query_iface_response) = self.query_iface(iface_id).await? {
            let phy = self.ensure_phy(query_iface_response.phy_id).await?;
            let iface_id = query_iface_response.id;

            // TODO(47383): PhyManager will add interfaces and instigate most of these events.
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

    /// Ensures that the `iface_id` is not present in any of the `PhyContainer` interface lists.
    pub fn on_iface_removed(&mut self, iface_id: u16) {
        for (_, phy_info) in self.phys.iter_mut() {
            phy_info.client_ifaces.remove(&iface_id);
            phy_info.ap_ifaces.remove(&iface_id);
        }
    }

    /// Returns a list of PHY IDs that can have interfaces of the requested MAC role.
    fn phys_for_role(&self, role: MacRole) -> Vec<u16> {
        self.phys
            .iter()
            .filter_map(|(k, v)| if v.phy_info.mac_roles.contains(&role) { Some(*k) } else { None })
            .collect()
    }

    /// Creates client interfaces for all PHYs that are capable of acting as clients.  For newly
    /// discovered PHYs, create client interfaces if the PHY can support them.
    pub async fn start_client_connections(&mut self) -> Result<(), PhyManagerError> {
        self.client_connections_enabled = true;

        let client_capable_phy_ids = self.phys_for_role(MacRole::Client);

        for client_phy in client_capable_phy_ids.iter() {
            let phy_container =
                self.phys.get_mut(&client_phy).ok_or(PhyManagerError::PhyQueryFailure)?;
            if phy_container.client_ifaces.is_empty() {
                let iface_id =
                    create_iface(&self.device_service, *client_phy, MacRole::Client).await?;
                phy_container.client_ifaces.insert(iface_id);
            }
        }

        Ok(())
    }

    /// Destroys all client interfaces.  Do not allow the creation of client interfaces for newly
    /// discovered PHYs.
    pub async fn stop_client_connections(&mut self) -> Result<(), PhyManagerError> {
        self.client_connections_enabled = false;

        let client_capable_phys = self.phys_for_role(MacRole::Client);

        for client_phy in client_capable_phys.iter() {
            let phy_container =
                self.phys.get_mut(&client_phy).ok_or(PhyManagerError::PhyQueryFailure)?;
            for iface_id in phy_container.client_ifaces.drain() {
                let mut request = fidl_service::DestroyIfaceRequest { iface_id: iface_id };
                match self.device_service.destroy_iface(&mut request).await {
                    Ok(status) => {
                        if fuchsia_zircon::ok(status).is_err() {
                            return Err(PhyManagerError::IfaceDestroyFailure);
                        }
                    }
                    Err(_) => return Err(PhyManagerError::IfaceDestroyFailure),
                }
            }
        }

        Ok(())
    }

    /// Finds a PHY with a client interface and returns the interface's ID to the caller.
    pub fn get_client(&mut self) -> Option<u16> {
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
}

/// Creates an interface of the requested role for the requested PHY ID.  Returns either the
/// ID of the created interface or an error.
async fn create_iface(
    proxy: &fidl_service::DeviceServiceProxy,
    phy_id: u16,
    role: MacRole,
) -> Result<u16, PhyManagerError> {
    let mut request = fidl_service::CreateIfaceRequest { phy_id, role };
    let create_iface_response = match proxy.create_iface(&mut request).await {
        Ok((status, iface_response)) => {
            if fuchsia_zircon::ok(status).is_err() || iface_response.is_none() {
                return Err(PhyManagerError::IfaceCreateFailure);
            }
            iface_response.ok_or_else(|| PhyManagerError::IfaceCreateFailure)?
        }
        Err(_) => return Err(PhyManagerError::IfaceCreateFailure),
    };
    Ok(create_iface_response.iface_id)
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
    fn create_phy_info(
        id: u16,
        dev_path: Option<String>,
        mac: [u8; 6],
        mac_roles: Vec<MacRole>,
    ) -> fidl_device::PhyInfo {
        fidl_device::PhyInfo {
            id: id,
            dev_path: dev_path,
            hw_mac_address: mac,
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
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = Vec::new();
        let phy_info = create_phy_info(
            fake_phy_id,
            fake_device_path.clone(),
            fake_mac_addr,
            fake_mac_roles.clone(),
        );

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
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles)
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
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = Vec::new();
        let phy_info = create_phy_info(
            fake_phy_id,
            fake_device_path.clone(),
            fake_mac_addr,
            fake_mac_roles.clone(),
        );

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
                create_phy_info(
                    fake_phy_id,
                    fake_device_path.clone(),
                    fake_mac_addr,
                    fake_mac_roles.clone()
                )
            );
        }

        // Send an update for the same PHY ID and ensure that the PHY info is updated.
        let updated_mac = [5, 4, 3, 2, 1, 0];
        let phy_info = create_phy_info(
            fake_phy_id,
            fake_device_path.clone(),
            updated_mac,
            fake_mac_roles.clone(),
        );

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
            create_phy_info(fake_phy_id, fake_device_path, updated_mac, fake_mac_roles)
        );
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnPhyRemoved event and
    /// calling remove_phy on PhyManager for a PHY that not longer exists.  The PhyManager in this
    /// case should remove the PhyContainer associated with the removed PHY ID.
    #[test]
    fn add_phy_after_start_client_connections() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = vec![MacRole::Client];
        let phy_info = create_phy_info(
            fake_phy_id,
            fake_device_path.clone(),
            fake_mac_addr,
            fake_mac_roles.clone(),
        );

        {
            let start_connections_fut = phy_manager.start_client_connections();
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
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = Vec::new();
        let phy_info =
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles);

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
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = Vec::new();
        let phy_info =
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles);

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
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = Vec::new();
        let phy_info =
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles);

        let phy_container = PhyContainer::new(phy_info);

        // Create an IfaceResponse to be sent to the PhyManager when the iface ID is queried
        let fake_role = MacRole::Client;
        let fake_iface_id = 1;
        let fake_phy_assigned_id = 1;
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
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = Vec::new();
        let phy_info =
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles);

        // Create an IfaceResponse to be sent to the PhyManager when the iface ID is queried
        let fake_role = MacRole::Client;
        let fake_iface_id = 1;
        let fake_phy_assigned_id = 1;
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
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = Vec::new();
        let phy_info =
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles);

        // Inject the fake PHY information
        let phy_container = PhyContainer::new(phy_info);
        phy_manager.phys.insert(fake_phy_id, phy_container);

        // Create an IfaceResponse to be sent to the PhyManager when the iface ID is queried
        let fake_role = MacRole::Client;
        let fake_iface_id = 1;
        let fake_phy_assigned_id = 1;
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
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = Vec::new();
        let phy_info =
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles);

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
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = Vec::new();
        let phy_info =
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles);

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
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = vec![MacRole::Client];
        let phy_info =
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles);
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
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = vec![MacRole::Client];
        let phy_info =
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles);
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
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = vec![MacRole::Ap];
        let phy_info =
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles);
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
    fn stop_client_connections() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = vec![MacRole::Client];
        let phy_info =
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles);
        let phy_container = PhyContainer::new(phy_info);

        {
            phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            phy_container.client_ifaces.insert(fake_iface_id);

            // Stop client connections
            let stop_clients_future = phy_manager.stop_client_connections();
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

    /// Tests the PhyManager's response to stop_client_connections when no client ifaces are
    /// present but an AP iface is present.  The expectation is that the AP iface is left intact.
    #[test]
    fn stop_client_connections_no_clients() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        // Create an initial PhyContainer to be inserted into the test PhyManager before the fake
        // iface is added.
        let fake_iface_id = 1;
        let fake_phy_id = 1;
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = vec![MacRole::Ap];
        let phy_info =
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles);
        let phy_container = PhyContainer::new(phy_info);

        // Insert the fake AP iface and then stop clients
        {
            phy_manager.phys.insert(fake_phy_id, phy_container);

            // Insert the fake AP iface
            let phy_container = phy_manager.phys.get_mut(&fake_phy_id).unwrap();
            phy_container.ap_ifaces.insert(fake_iface_id);

            // Stop client connections
            let stop_clients_future = phy_manager.stop_client_connections();
            pin_mut!(stop_clients_future);

            assert!(exec.run_until_stalled(&mut stop_clients_future).is_ready());
        }

        // Ensure that the fake PHY and AP interface are still present.
        assert!(phy_manager.phys.contains_key(&fake_phy_id));

        let phy_container = phy_manager.phys.get(&fake_phy_id).unwrap();
        assert!(phy_container.ap_ifaces.contains(&fake_iface_id));
    }
}
