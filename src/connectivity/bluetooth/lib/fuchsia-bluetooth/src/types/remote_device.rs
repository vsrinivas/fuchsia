// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_control as control;

#[derive(Clone, Debug, PartialEq)]
pub struct RemoteDevice {
    pub identifier: String,
    pub address: String,
    pub technology: control::TechnologyType,
    pub name: Option<String>,
    pub appearance: control::Appearance,
    pub rssi: Option<i8>,
    pub tx_power: Option<i8>,
    pub connected: bool,
    pub bonded: bool,
    pub service_uuids: Vec<String>,
}

impl From<control::RemoteDevice> for RemoteDevice {
    fn from(r: control::RemoteDevice) -> RemoteDevice {
        RemoteDevice {
            identifier: r.identifier,
            address: r.address,
            technology: r.technology,
            name: r.name,
            appearance: r.appearance,
            rssi: r.rssi.map(|r| r.value),
            tx_power: r.tx_power.map(|t| t.value),
            connected: r.connected,
            bonded: r.bonded,
            service_uuids: r.service_uuids,
        }
    }
}

impl From<RemoteDevice> for control::RemoteDevice {
    fn from(r: RemoteDevice) -> control::RemoteDevice {
        control::RemoteDevice {
            identifier: r.identifier,
            address: r.address,
            technology: r.technology,
            name: r.name,
            appearance: r.appearance,
            rssi: r.rssi.map(|r| Box::new(fidl_fuchsia_bluetooth::Int8 { value: r })),
            tx_power: r.tx_power.map(|t| Box::new(fidl_fuchsia_bluetooth::Int8 { value: t })),
            connected: r.connected,
            bonded: r.bonded,
            service_uuids: r.service_uuids,
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_bluetooth_control as control};

    #[test]
    fn remote_device_conversion() {
        let peer = RemoteDevice {
            identifier: "id".into(),
            address: "addr".into(),
            technology: control::TechnologyType::DualMode,
            name: None,
            appearance: control::Appearance::Computer,
            rssi: Some(-75),
            tx_power: Some(127),
            connected: true,
            bonded: false,
            service_uuids: vec![],
        };
        assert_eq!(peer, RemoteDevice::from(control::RemoteDevice::from(peer.clone())));
    }

    #[test]
    fn get_remote_device_id() {
        let identifier = "addr";
        let peer = RemoteDevice {
            identifier: identifier.into(),
            address: "addr".into(),
            technology: control::TechnologyType::DualMode,
            name: None,
            appearance: control::Appearance::Computer,
            rssi: Some(-75),
            tx_power: Some(127),
            connected: true,
            bonded: false,
            service_uuids: vec![],
        };
        assert_eq!(peer.identifier, identifier);
    }
}
