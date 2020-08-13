// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_hardware_ethernet_ext;

use crate::error;

const INTF_METRIC_WLAN: u32 = 90;
const INTF_METRIC_ETH: u32 = 100;

#[derive(Debug)]
pub enum Action {
    ADD,
    REMOVE,
}

#[derive(Debug)]
pub struct OIRInfo {
    pub action: Action,
    pub file_path: String,
    pub topological_path: String,
    pub device_information: Option<fidl_fuchsia_hardware_ethernet_ext::EthernetInfo>,
    pub device_channel: Option<fuchsia_zircon::Channel>,
}

#[derive(Debug)]
pub struct PortDevice {
    pub name: String,
    pub file_path: String,
    pub topological_path: String,
    pub metric: u32,
}

impl PortDevice {
    /// Returns a `PortDevice` based on the information passed.
    pub fn new(
        file_path: &str,
        topological_path: &str,
        device_information: fidl_fuchsia_hardware_ethernet_ext::EthernetInfo,
    ) -> error::Result<Self> {
        info!("add interface: {} {} {:?}", file_path, topological_path, device_information);
        let mut persisted_interface_config =
            interface::FileBackedConfig::load(&"/data/net_interfaces.cfg.json").unwrap();

        let name = persisted_interface_config
            .get_stable_name(
                &topological_path,
                device_information.mac,
                device_information
                    .features
                    .contains(fidl_fuchsia_hardware_ethernet::Features::Wlan),
            )
            .unwrap();
        // Hardcode the interface metric. Eventually this should
        // be part of the config file.
        let metric = match device_information
            .features
            .contains(fidl_fuchsia_hardware_ethernet::Features::Wlan)
        {
            true => INTF_METRIC_WLAN,
            false => INTF_METRIC_ETH,
        };
        let port = PortDevice {
            name: name.to_string(),
            topological_path: topological_path.to_string(),
            file_path: file_path.to_string(),
            metric,
        };
        Ok(port)
    }
}

pub fn remove_interface(topological_path: &str) {
    info!("remove interface: {:?} not implemented", topological_path);
}
