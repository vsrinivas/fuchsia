// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_wlan_device::{ConnectorProxy, PhyProxy};
use fidl_fuchsia_wlan_device_service::{DeviceServiceMarker, DeviceServiceProxy};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
use std::fs;

#[derive(Debug)]
pub struct WlanPhyFacade {
    device_service: DeviceServiceProxy,
}

impl WlanPhyFacade {
    pub fn new() -> Result<WlanPhyFacade, Error> {
        Ok(WlanPhyFacade { device_service: connect_to_service::<DeviceServiceMarker>()? })
    }

    /// Queries the currently counfigured country from phy `phy_id`.
    ///
    /// # Arguments
    /// * `phy_id`: a u16 id representing the phy
    pub async fn get_country(&self, phy_id: u16) -> Result<[u8; 2], Error> {
        let phy_list = self
            .device_service
            .list_phys()
            .await
            .context("get_country(): failed to enumerate phys")?
            .phys;
        let phy = phy_list
            .iter()
            .find(|phy| phy.phy_id == phy_id)
            .ok_or_else(|| format_err!("get_country(): failed to find phy with id {}", phy_id))?;

        let phy_path = &phy.path;
        let phy_node = fs::File::open(phy_path).with_context(|| {
            format_err!("get_country(): failed to open Phy node at path {}", phy_path)
        })?;
        let (local, remote) = zx::Channel::create()?;
        let connector_channel = fdio::clone_channel(&phy_node)?;
        let connector = ConnectorProxy::new(
            fasync::Channel::from_channel(connector_channel)
                .context("get_country(): failed to create channel for Connector")?,
        );
        connector.connect(ServerEnd::new(remote))?;

        let phy_proxy = PhyProxy::new(
            fasync::Channel::from_channel(local)
                .context("get_country(): failed to create channel for PhyProxy")?,
        );
        let country_code =
            phy_proxy.get_country().await.context("get_country(): encountered FIDL error")?;
        match country_code {
            Ok(country) => Ok(country.alpha2),
            Err(status) => Err(format_err!(
                "get_country(): encountered service failure {}",
                zx::Status::from_raw(status)
            )),
        }
    }
}
