// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{assigned_numbers::find_service_uuid, inspect::*},
    fidl_fuchsia_bluetooth_control as control, fuchsia_inspect as inspect,
    fuchsia_inspect_contrib::nodes::ManagedNode,
    std::fmt::{self, Write},
};

impl ImmutableDataInspect<Peer> for ImmutableDataInspectManager {
    fn new(data: &Peer, mut manager: ManagedNode) -> ImmutableDataInspectManager {
        let mut writer = manager.writer();
        writer.create_string("technology", &data.technology.debug());
        writer.create_string("appearance", &data.appearance.debug());
        if let Some(rssi) = data.rssi {
            writer.create_int("rssi", rssi as i64);
        }
        if let Some(tx_power) = data.tx_power {
            writer.create_int("tx_power", tx_power as i64);
        }
        writer.create_uint("connected", data.connected.to_property());
        writer.create_uint("bonded", data.bonded.to_property());
        if !data.service_uuids.is_empty() {
            writer.create_string("service_uuids", &data.service_uuids.to_property());
        }
        Self { _manager: manager }
    }
}

impl IsInspectable for Peer {
    type I = ImmutableDataInspectManager;
}

#[derive(Clone, Debug, PartialEq)]
pub struct Peer {
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

impl Peer {
    /// Construct a concise summary of a `Peer`'s information.
    pub fn summary(&self) -> String {
        let mut msg = String::new();
        write!(msg, "Device {}", self.address).expect("Error occurred writing to String");
        if let Some(rssi) = &self.rssi {
            write!(msg, ", RSSI {}", rssi).expect("Error occurred writing to String");
        }
        if let Some(name) = &self.name {
            write!(msg, ", Name {}", name).expect("Error occurred writing to String");
        }
        if self.bonded {
            write!(msg, " [bonded]").expect("Error occurred writing to String");
        }
        if self.connected {
            write!(msg, " [connected]").expect("Error occurred writing to String");
        }
        msg
    }
}

impl fmt::Display for Peer {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(fmt, "Peer:")?;
        writeln!(fmt, "\tIdentifier:\t{}", self.identifier)?;
        writeln!(fmt, "\tAddress:\t{}", self.address)?;
        writeln!(fmt, "\tTechnology:\t{:?}", self.technology)?;
        if let Some(name) = &self.name {
            writeln!(fmt, "\tName:\t\t{}", name)?;
        }
        writeln!(fmt, "\tAppearance:\t{:?}", self.appearance)?;
        if let Some(rssi) = &self.rssi {
            writeln!(fmt, "\tRSSI:\t\t{}", rssi)?;
        }
        if let Some(tx_power) = &self.tx_power {
            writeln!(fmt, "\tTX Power:\t{}", tx_power)?;
        }
        writeln!(fmt, "\tConnected:\t{}", self.connected)?;
        writeln!(fmt, "\tBonded:\t\t{}", self.bonded)?;
        writeln!(
            fmt,
            "\tServices:\t{:?}",
            self.service_uuids
                .iter()
                .map(|uuid| find_service_uuid(uuid).map(|an| an.name).unwrap_or(uuid))
                .collect::<Vec<_>>(),
        )?;
        Ok(())
    }
}

impl From<control::RemoteDevice> for Peer {
    fn from(r: control::RemoteDevice) -> Peer {
        Peer {
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

impl From<Peer> for control::RemoteDevice {
    fn from(p: Peer) -> control::RemoteDevice {
        control::RemoteDevice {
            identifier: p.identifier,
            address: p.address,
            technology: p.technology,
            name: p.name,
            appearance: p.appearance,
            rssi: p.rssi.map(|r| Box::new(fidl_fuchsia_bluetooth::Int8 { value: r })),
            tx_power: p.tx_power.map(|t| Box::new(fidl_fuchsia_bluetooth::Int8 { value: t })),
            connected: p.connected,
            bonded: p.bonded,
            service_uuids: p.service_uuids,
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_bluetooth_control as control};

    #[test]
    fn remote_device_conversion() {
        let peer = Peer {
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
        assert_eq!(peer, Peer::from(control::RemoteDevice::from(peer.clone())));
    }

    #[test]
    fn get_peer_id() {
        let identifier = "addr";
        let peer = Peer {
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
