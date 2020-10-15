// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::wlan::types::{ClientStatusResponse, MacRole, QueryIfaceResponse};
use anyhow::{Context as _, Error};
use fidl_fuchsia_wlan_device;
use fidl_fuchsia_wlan_device_service::{DeviceServiceMarker, DeviceServiceProxy};
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
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
        let sme_proxy = wlan_service_util::client::get_first_sme(&self.wlan_svc)
            .await
            .context("Scan: failed to get client iface sme proxy")?;

        // start the scan
        let results =
            wlan_service_util::client::passive_scan(&sme_proxy).await.context("Scan failed")?;

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
        let sme_proxy = wlan_service_util::client::get_first_sme(&self.wlan_svc)
            .await
            .context("Connect: failed to get client iface sme proxy")?;

        wlan_service_util::client::connect(&sme_proxy, target_ssid, target_pwd).await
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
        wlan_service_util::client::disconnect_all(&self.wlan_svc)
            .await
            .context("Disconnect: Failed to disconnect ifaces")
    }

    pub async fn status(&self) -> Result<ClientStatusResponse, Error> {
        // get the first client interface
        let sme_proxy = wlan_service_util::client::get_first_sme(&self.wlan_svc)
            .await
            .context("Status: failed to get iface sme proxy")?;

        let rsp = sme_proxy.status().await.context("failed to get status from sme_proxy")?;

        Ok(ClientStatusResponse::from(rsp))
    }

    pub async fn query_iface(&self, iface_id: u16) -> Result<QueryIfaceResponse, Error> {
        let (status, iface_info) = self
            .wlan_svc
            .query_iface(iface_id)
            .await
            .context("Failed to query iface information")?;

        zx::ok(status)?;

        let iface_info = match iface_info {
            Some(iface_info) => iface_info,
            None => return Err(format_err!("no iface information for ID: {}", iface_id)),
        };
        let mac_role = match iface_info.role {
            fidl_fuchsia_wlan_device::MacRole::Client => MacRole::Client,
            fidl_fuchsia_wlan_device::MacRole::Ap => MacRole::Ap,
            fidl_fuchsia_wlan_device::MacRole::Mesh => MacRole::Mesh,
        };

        Ok(QueryIfaceResponse {
            role: mac_role,
            id: iface_info.id,
            phy_id: iface_info.phy_id,
            phy_assigned_id: iface_info.phy_assigned_id,
            mac_addr: iface_info.mac_addr,
        })
    }
}
