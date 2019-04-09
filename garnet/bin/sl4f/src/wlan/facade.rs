// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use connectivity_testing::wlan_service_util;
use failure::{Error, ResultExt};
use fidl_fuchsia_wlan_device_service::{DeviceServiceMarker, DeviceServiceProxy};
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog::fx_log_err;
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

    pub async fn scan(&self) -> Result<Vec<String>, Error> {
        // get iface info
        let wlan_iface_ids = await!(wlan_service_util::get_iface_list(&self.wlan_svc))
            .context("Scan: failed to get wlan iface list")?;

        if wlan_iface_ids.len() == 0 {
            bail!("no wlan interfaces found");
        }

        // pick the first one
        let sme_proxy =
            await!(wlan_service_util::get_iface_sme_proxy(&self.wlan_svc, wlan_iface_ids[0]))
                .context("Scan: failed to get iface sme proxy")?;

        // start the scan
        let results = await!(wlan_service_util::perform_scan(&sme_proxy)).context("Scan failed")?;

        // send the ssids back to the test
        let mut ssids = Vec::new();
        for entry in &results {
            let ssid = String::from_utf8_lossy(&entry.best_bss.ssid).into_owned();
            ssids.push(ssid);
        }

        Ok(ssids)
    }

    pub async fn connect(&self, target_ssid: Vec<u8>, target_pwd: Vec<u8>) -> Result<bool, Error> {
        // get iface info
        let wlan_iface_ids = await!(wlan_service_util::get_iface_list(&self.wlan_svc))
            .context("Connect: failed to get wlan iface list")?;

        if wlan_iface_ids.len() == 0 {
            bail!("no wlan interfaces found");
        }

        // pick the first one
        let sme_proxy =
            await!(wlan_service_util::get_iface_sme_proxy(&self.wlan_svc, wlan_iface_ids[0]))
                .context("Connect: failed to get iface sme proxy")?;

        await!(wlan_service_util::connect_to_network(&sme_proxy, target_ssid, target_pwd))
    }

    pub async fn disconnect(&self) -> Result<(), Error> {
        // get iface info
        let wlan_iface_ids = await!(wlan_service_util::get_iface_list(&self.wlan_svc))
            .context("Disconnect: failed to get wlan iface list")?;

        if wlan_iface_ids.len() == 0 {
            bail!("no wlan interfaces found");
        }

        let mut disconnect_error = false;

        // disconnect all networks
        for iface_id in wlan_iface_ids {
            let sme_proxy =
                await!(wlan_service_util::get_iface_sme_proxy(&self.wlan_svc, iface_id))
                    .context("Disconnect: failed to get iface sme proxy")?;

            match await!(wlan_service_util::disconnect_from_network(&sme_proxy)) {
                Err(e) => {
                    fx_log_err!("Disconnect call failed on iface {}: {:?}", iface_id, e);
                    disconnect_error = true;
                }
                _ => {}
            }
        }
        if disconnect_error {
            bail!("saw a failure with at least one disconnect call");
        }
        Ok(())
    }
}
