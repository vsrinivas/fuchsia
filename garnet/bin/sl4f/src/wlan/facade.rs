// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::wlan::types::{
    ClientStateSummary, ConnectionState, DisconnectStatus, NetworkIdentifier, NetworkState,
    SecurityType, WlanClientState,
};
use anyhow::{Context as _, Error};
use connectivity_testing::wlan_service_util;
use fidl_fuchsia_wlan_device_service::{DeviceServiceMarker, DeviceServiceProxy};
use fuchsia_component::client::connect_to_service;
use parking_lot::RwLock;

// WlanFacade: proxies commands from sl4f test to proper fidl APIs
//
// This object is shared among all threads created by server.  The inner object is the facade
// itself.  Callers interact with a wrapped version of the facade that enforces read/write
// protection.
//
// Use: Create once per server instantiation.
#[derive(Debug)]
struct InnerWlanFacade {
    scan_results: bool,
}

#[derive(Debug)]
pub struct WlanFacade {
    wlan_svc: DeviceServiceProxy,

    inner: RwLock<InnerWlanFacade>,
}

impl WlanFacade {
    pub fn new() -> Result<WlanFacade, Error> {
        let wlan_svc = connect_to_service::<DeviceServiceMarker>()?;

        Ok(WlanFacade { wlan_svc, inner: RwLock::new(InnerWlanFacade { scan_results: false }) })
    }

    /// Gets the list of wlan interface IDs.
    pub async fn get_iface_id_list(&self) -> Result<Vec<u16>, Error> {
        let wlan_iface_ids = wlan_service_util::get_iface_list(&self.wlan_svc)
            .await
            .context("Get Iface Id List: failed to get wlan iface list")?;
        Ok(wlan_iface_ids)
    }

    /// Gets the list of wlan interface IDs.
    pub async fn get_phy_id_list(&self) -> Result<Vec<u16>, Error> {
        let wlan_phy_ids = wlan_service_util::get_phy_list(&self.wlan_svc)
            .await
            .context("Get Phy Id List: failed to get wlan phy list")?;
        Ok(wlan_phy_ids)
    }

    pub async fn scan(&self) -> Result<Vec<String>, Error> {
        // get the first client interface
        let sme_proxy = wlan_service_util::get_first_client_sme(&self.wlan_svc)
            .await
            .context("Scan: failed to get client iface sme proxy")?;

        // start the scan
        let results = wlan_service_util::perform_scan(&sme_proxy).await.context("Scan failed")?;

        // send the ssids back to the test
        let mut ssids = Vec::new();
        for entry in &results {
            let ssid = String::from_utf8_lossy(&entry.ssid).into_owned();
            ssids.push(ssid);
        }
        Ok(ssids)
    }

    pub async fn connect(&self, target_ssid: Vec<u8>, target_pwd: Vec<u8>) -> Result<bool, Error> {
        // get the first client interface
        let sme_proxy = wlan_service_util::get_first_client_sme(&self.wlan_svc)
            .await
            .context("Connect: failed to get client iface sme proxy")?;

        wlan_service_util::connect_to_network(&sme_proxy, target_ssid, target_pwd).await
    }

    /// Destroys a WLAN interface by input interface ID.
    ///
    /// # Arguments
    /// * `iface_id` - The u16 interface id.
    pub async fn destroy_iface(&self, iface_id: u16) -> Result<(), Error> {
        wlan_service_util::destroy_iface(&self.wlan_svc, iface_id)
            .await
            .context("Destroy: Failed to destroy iface")
    }

    pub async fn disconnect(&self) -> Result<(), Error> {
        wlan_service_util::disconnect_all_clients(&self.wlan_svc)
            .await
            .context("Disconnect: Failed to disconnect ifaces")
    }

    pub async fn status(&self) -> Result<ClientStateSummary, Error> {
        // get the first client interface
        let sme_proxy = wlan_service_util::get_first_client_sme(&self.wlan_svc)
            .await
            .context("Status: failed to get iface sme proxy")?;

        let rsp = sme_proxy.status().await.context("failed to get status from sme_proxy")?;

        // Create dummy ClientStateSummary
        let mut connection_state = ConnectionState::Disconnected;
        let mut network_id = NetworkIdentifier { ssid: vec![], type_: SecurityType::None };

        match rsp.connected_to {
            Some(ref bss) => {
                network_id.ssid = bss.ssid.as_slice().to_vec();
                connection_state = ConnectionState::Connected;
            }
            _ => {}
        }

        let network_state = NetworkState {
            id: Some(network_id),
            state: Some(connection_state),
            status: Some(DisconnectStatus::ConnectionFailed),
        };

        let mut networks = Vec::new();
        networks.push(network_state);

        let client_state_summary = ClientStateSummary {
            state: Some(WlanClientState::ConnectionsEnabled),
            networks: Some(networks),
        };

        Ok(client_state_summary)
    }
}
