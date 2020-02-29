// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use {
    core::hash::Hash, fidl_fuchsia_wlan_device, fidl_fuchsia_wlan_device_service, fuchsia_zircon,
    std::collections::HashMap, thiserror::Error,
};

/// Errors raised while attempting to query information about or configure PHYs and ifaces.
#[derive(Debug, Error)]
enum PhyManagerError {
    #[error("unable to query phy information")]
    PhyQueryFailure,
}

/// Holds the ID of ifaces that are added by the device watcher.
#[derive(Clone, Copy, Hash, PartialEq, Eq)]
struct IfaceId(u16);

/// Holds the ID of PHYs that are added by the device watcher.
#[derive(Clone, Copy, Hash, PartialEq, Eq)]
struct PhyId(u16);

/// Stores information about a WLAN PHY and any interfaces that belong to it.
struct PhyContainer {
    phy_info: fidl_fuchsia_wlan_device::PhyInfo,
    client_ifaces: Vec<IfaceId>,
    ap_ifaces: Vec<IfaceId>,
}

/// Maintains a record of all PHYs that are present and their associated interfaces.
struct PhyManager {
    phys: HashMap<PhyId, PhyContainer>,
    device_service: fidl_fuchsia_wlan_device_service::DeviceServiceProxy,
}

impl PhyContainer {
    /// Stores the PhyInfo associated with a newly discovered PHY and creates empty vectors to hold
    /// interface IDs that belong to this PHY.
    pub fn new(phy_info: fidl_fuchsia_wlan_device::PhyInfo) -> Self {
        PhyContainer { phy_info: phy_info, client_ifaces: Vec::new(), ap_ifaces: Vec::new() }
    }
}

impl PhyManager {
    /// Internally stores a DeviceServiceProxy to query PHY and interface properties and create and
    /// destroy interfaces as requested.
    pub fn new(device_service: fidl_fuchsia_wlan_device_service::DeviceServiceProxy) -> Self {
        PhyManager { phys: HashMap::new(), device_service: device_service }
    }

    /// Checks to see if this PHY is already accounted for.  If it is not, queries its PHY
    /// attributes and places it in the hash map.
    pub async fn add_phy(&mut self, phy_id: u16) -> Result<(), PhyManagerError> {
        let query_phy_response = match self
            .device_service
            .query_phy(&mut fidl_fuchsia_wlan_device_service::QueryPhyRequest { phy_id: phy_id })
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
            println!("wlancfg: adding PHY ID #{}", phy_id);
            self.phys.insert(PhyId(response.info.id), PhyContainer::new(response.info));
        }
        Ok(())
    }

    /// If the PHY is accounted for, removes the associated `PhyContainer` from the hash map.
    pub fn remove_phy(&mut self, phy_id: u16) {
        self.phys.remove(&PhyId(phy_id));
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints,
        fidl_fuchsia_wlan_device, fidl_fuchsia_wlan_device_service,
        fuchsia_async::Executor,
        fuchsia_zircon::sys::{ZX_ERR_NOT_FOUND, ZX_OK},
        futures::stream::StreamExt,
        futures::task::Poll,
        pin_utils::pin_mut,
    };

    /// Hold the client and service ends for DeviceService to allow mocking DeviceService responses
    /// for unit tests.
    struct TestValues {
        proxy: fidl_fuchsia_wlan_device_service::DeviceServiceProxy,
        stream: fidl_fuchsia_wlan_device_service::DeviceServiceRequestStream,
    }

    /// Create a TestValues for a unit test.
    fn test_setup() -> TestValues {
        let (proxy, requests) =
            endpoints::create_proxy::<fidl_fuchsia_wlan_device_service::DeviceServiceMarker>()
                .expect("failed to create SeviceService proxy");
        let stream = requests.into_stream().expect("failed to create stream");

        TestValues { proxy: proxy, stream: stream }
    }

    /// Take in the service side of a DeviceService::QueryPhy request and respond with the given
    /// PhyInfo.
    fn send_query_phy_response(
        exec: &mut Executor,
        server: &mut fidl_fuchsia_wlan_device_service::DeviceServiceRequestStream,
        phy_info: Option<fidl_fuchsia_wlan_device::PhyInfo>,
    ) {
        let poll = exec.run_until_stalled(&mut server.next());
        let request = match poll {
            Poll::Ready(poll_ready) => poll_ready
                .expect("poll ready result is None")
                .expect("poll ready result is an Error"),
            Poll::Pending => panic!("no DeviceService request available"),
        };

        let responder = match request {
            fidl_fuchsia_wlan_device_service::DeviceServiceRequest::QueryPhy {
                responder, ..
            } => responder,
            _ => panic!("expecting a QueryPhy request"),
        };

        match phy_info {
            Some(phy_info) => responder
                .send(
                    ZX_OK,
                    Some(&mut fidl_fuchsia_wlan_device_service::QueryPhyResponse {
                        info: phy_info,
                    }),
                )
                .expect("sending fake phy info"),
            None => {
                responder.send(ZX_ERR_NOT_FOUND, None).expect("sending fake response with none")
            }
        }
    }

    /// Create a PhyInfo object for unit testing.
    fn create_phy_info(
        id: PhyId,
        dev_path: Option<String>,
        mac: [u8; 6],
        mac_roles: Vec<fidl_fuchsia_wlan_device::MacRole>,
    ) -> fidl_fuchsia_wlan_device::PhyInfo {
        fidl_fuchsia_wlan_device::PhyInfo {
            id: id.0,
            dev_path: dev_path,
            hw_mac_address: mac,
            supported_phys: Vec::new(),
            driver_features: Vec::new(),
            mac_roles: mac_roles,
            caps: Vec::new(),
            bands: Vec::new(),
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

        let fake_phy_id = PhyId(1);
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

        let fake_phy_id = PhyId(1);
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
    fn remove_valid_phy() {
        let _exec = Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        let fake_phy_id = PhyId(1);
        let fake_device_path = Some("/test/path".to_string());
        let fake_mac_addr = [0, 1, 2, 3, 4, 5];
        let fake_mac_roles = Vec::new();
        let phy_info =
            create_phy_info(fake_phy_id, fake_device_path, fake_mac_addr, fake_mac_roles);

        let phy_container = PhyContainer::new(phy_info);
        phy_manager.phys.insert(fake_phy_id, phy_container);
        phy_manager.remove_phy(fake_phy_id.0);
        assert!(phy_manager.phys.is_empty());
    }

    /// This test mimics a client of the DeviceWatcher watcher receiving an OnPhyRemoved event and
    /// calling remove_phy on PhyManager for a PHY ID that is not accounted for by the PhyManager.
    /// The PhyManager should realize that it is unaware of this PHY ID and leave its PhyContainers
    /// unchanged.
    #[test]
    fn remove_nonexistent_phy() {
        let _exec = Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let mut phy_manager = PhyManager::new(test_values.proxy);

        let fake_phy_id = PhyId(1);
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
}
