// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mode_management::phy_manager::PhyManagerApi,
    eui48::MacAddress,
    fidl_fuchsia_wlan_product_deprecatedconfiguration as fidl_deprecated,
    futures::{lock::Mutex, select, StreamExt},
    log::{error, info},
    std::sync::Arc,
};

#[derive(Clone)]
pub struct DeprecatedConfigurator {
    phy_manager: Arc<Mutex<dyn PhyManagerApi + Send>>,
}

impl DeprecatedConfigurator {
    pub fn new(phy_manager: Arc<Mutex<dyn PhyManagerApi + Send>>) -> Self {
        DeprecatedConfigurator { phy_manager }
    }

    pub async fn serve_deprecated_configuration(
        self,
        mut requests: fidl_deprecated::DeprecatedConfiguratorRequestStream,
    ) {
        loop {
            select! {
                req = requests.select_next_some() => match req {
                    Ok(req) => match req {
                        fidl_deprecated::DeprecatedConfiguratorRequest::SuggestAccessPointMacAddress{mac, responder} => {
                            info!("setting suggested AP MAC");
                            let mac = match MacAddress::from_bytes(&mac.octets) {
                                Ok(mac) => mac,
                                Err(e) => {
                                    error!("failed to parse MAC address: {:?}", e);
                                    match responder.send(
                                        &mut Err(fidl_deprecated::SuggestMacAddressError::InvalidArguments)
                                    ) {
                                        Ok(()) => {},
                                        Err(e) => error!(
                                            "could not send SuggestAccessPointMacAddress response: {:?}", e
                                        )
                                    }
                                    continue;
                                }
                            };
                            let mut phy_manager = self.phy_manager.lock().await;
                            phy_manager.suggest_ap_mac(mac);

                            match responder.send(&mut Ok(())) {
                                Ok(()) => {}
                                Err(e) => {
                                    error!("could not send SuggestAccessPointMacAddress response: {:?}", e);
                                }
                            }
                        }
                    }
                    Err(e) => error!("encountered an error while serving deprecated configuration requests: {}", e)
                },
                complete => break,
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            mode_management::{
                phy_manager::{CreateClientIfacesReason, PhyManagerError},
                Defect,
            },
            regulatory_manager::REGION_CODE_LEN,
        },
        async_trait::async_trait,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_net::MacAddress,
        fidl_fuchsia_wlan_common as fidl_common, fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::task::Poll,
        pin_utils::pin_mut,
        std::unimplemented,
        wlan_common::assert_variant,
    };

    #[derive(Debug)]
    struct StubPhyManager(Option<eui48::MacAddress>);

    impl StubPhyManager {
        fn new() -> Self {
            StubPhyManager(None)
        }
    }

    #[async_trait]
    impl PhyManagerApi for StubPhyManager {
        async fn add_phy(&mut self, _phy_id: u16) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn remove_phy(&mut self, _phy_id: u16) {
            unimplemented!();
        }

        async fn on_iface_added(&mut self, _iface_id: u16) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn on_iface_removed(&mut self, _iface_id: u16) {
            unimplemented!();
        }

        async fn create_all_client_ifaces(
            &mut self,
            _reason: CreateClientIfacesReason,
        ) -> Result<Vec<u16>, (Vec<u16>, PhyManagerError)> {
            unimplemented!();
        }

        fn client_connections_enabled(&self) -> bool {
            unimplemented!();
        }

        async fn destroy_all_client_ifaces(&mut self) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn get_client(&mut self) -> Option<u16> {
            unimplemented!();
        }

        fn get_wpa3_capable_client(&mut self) -> Option<u16> {
            unimplemented!();
        }

        async fn create_or_get_ap_iface(&mut self) -> Result<Option<u16>, PhyManagerError> {
            unimplemented!();
        }

        async fn destroy_ap_iface(&mut self, _iface_id: u16) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        async fn destroy_all_ap_ifaces(&mut self) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn suggest_ap_mac(&mut self, mac: eui48::MacAddress) {
            self.0 = Some(mac);
        }

        fn get_phy_ids(&self) -> Vec<u16> {
            unimplemented!();
        }

        fn log_phy_add_failure(&mut self) {
            unimplemented!();
        }

        async fn set_country_code(
            &mut self,
            _country_code: Option<[u8; REGION_CODE_LEN]>,
        ) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn has_wpa3_client_iface(&self) -> bool {
            unimplemented!();
        }

        async fn set_power_state(
            &mut self,
            _low_power_enabled: fidl_common::PowerSaveType,
        ) -> Result<zx::Status, anyhow::Error> {
            unimplemented!();
        }

        async fn record_defect(&mut self, _defect: Defect) {
            unimplemented!();
        }
    }

    #[fuchsia::test]
    fn test_suggest_mac_succeeds() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");

        // Set up the DeprecatedConfigurator.
        let phy_manager = Arc::new(Mutex::new(StubPhyManager::new()));
        let configurator = DeprecatedConfigurator::new(phy_manager.clone());

        // Create the request stream and proxy.
        let (configurator_proxy, remote) =
            create_proxy::<fidl_deprecated::DeprecatedConfiguratorMarker>()
                .expect("error creating proxy");
        let stream = remote.into_stream().expect("error creating proxy");

        // Kick off the serve loop and wait for it to stall out waiting for requests.
        let fut = configurator.serve_deprecated_configuration(stream);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Issue a request to set the MAC address.
        let octets = [1, 2, 3, 4, 5, 6];
        let mut mac = MacAddress { octets: octets };
        let mut suggest_fut = configurator_proxy.suggest_access_point_mac_address(&mut mac);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Verify that the MAC has been set on the PhyManager
        let lock_fut = phy_manager.lock();
        pin_mut!(lock_fut);
        let phy_manager = assert_variant!(
            exec.run_until_stalled(&mut lock_fut),
            Poll::Ready(phy_manager) => {
                phy_manager
            }
        );
        let expected_mac = eui48::MacAddress::from_bytes(&octets).unwrap();
        assert_eq!(Some(expected_mac), phy_manager.0);

        assert_variant!(exec.run_until_stalled(&mut suggest_fut), Poll::Ready(Ok(Ok(()))));
    }
}
