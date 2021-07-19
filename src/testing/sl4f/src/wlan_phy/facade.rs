// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context, Error};
use fidl_fuchsia_wlan_device_service::{DeviceMonitorMarker, DeviceMonitorProxy};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon as zx;

#[derive(Debug)]
pub struct WlanPhyFacade {
    device_monitor: DeviceMonitorProxy,
}

impl WlanPhyFacade {
    pub fn new() -> Result<WlanPhyFacade, Error> {
        Ok(WlanPhyFacade { device_monitor: connect_to_protocol::<DeviceMonitorMarker>()? })
    }

    /// Queries the currently counfigured country from phy `phy_id`.
    ///
    /// # Arguments
    /// * `phy_id`: a u16 id representing the phy
    pub async fn get_country(&self, phy_id: u16) -> Result<[u8; 2], Error> {
        let country_code = self
            .device_monitor
            .get_country(phy_id)
            .await
            .context("get_country(): encountered FIDL error")?;
        match country_code {
            Ok(country) => Ok(country.alpha2),
            Err(status) => Err(format_err!(
                "get_country(): encountered service failure {}",
                zx::Status::from_raw(status)
            )),
        }
    }

    /// Queries the device path of the PHY specified by `phy_id`.
    ///
    /// # Arguments
    /// * `phy_id`: a u16 id representing the phy
    pub async fn get_dev_path(&self, phy_id: u16) -> Result<Option<String>, Error> {
        self.device_monitor
            .get_dev_path(phy_id)
            .await
            .map_err(|e| format_err!("get_path(): encountered FIDL error: {:?}", e))
    }
}
