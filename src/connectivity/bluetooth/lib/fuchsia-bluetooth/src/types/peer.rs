// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        assigned_numbers::find_service_uuid,
        inspect::*,
        types::{Address, PeerId, Uuid},
    },
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth::{Appearance, DeviceClass},
    fidl_fuchsia_bluetooth_control as fctrl, fidl_fuchsia_bluetooth_sys as fsys,
    fuchsia_inspect as inspect,
    fuchsia_inspect_contrib::nodes::ManagedNode,
    std::{
        convert::TryFrom,
        fmt::{self, Write},
        str::FromStr,
    },
};

#[derive(Clone, Debug, PartialEq)]
pub struct Peer {
    /// Uniquely identifies this peer on the current system.
    pub id: PeerId,

    /// Bluetooth device address that identifies this peer. Clients
    /// should display this field to the user when |name| is not available.
    ///
    /// NOTE: Clients should use the `id` field to keep track of peers instead of their
    /// address.
    pub address: Address,

    /// The Bluetooth technologies that are supported by this peer.
    pub technology: fsys::TechnologyType,

    /// Whether or not a BR/EDR and/or LE connection exists to this peer.
    pub connected: bool,

    /// Whether or not this peer is bonded.
    pub bonded: bool,

    /// The name of the peer, if known.
    pub name: Option<String>,

    /// The LE appearance property. Present if this peer supports LE and the
    /// appearance information was obtained over advertising and/or GATT.
    pub appearance: Option<Appearance>,

    /// The class of device for this device, if known.
    pub device_class: Option<DeviceClass>,

    /// The most recently obtained advertising signal strength for this peer. Present if known.
    pub rssi: Option<i8>,

    /// The most recently obtained transmission power for this peer. Present if known.
    pub tx_power: Option<i8>,

    /// The list of service UUIDs known to be available on this peer.
    pub services: Vec<Uuid>,
}

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
        if !data.services.is_empty() {
            writer.create_string("services", &data.services.to_property());
        }
        Self { _manager: manager }
    }
}

impl IsInspectable for Peer {
    type I = ImmutableDataInspectManager;
}

impl fmt::Display for Peer {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(fmt, "Peer:")?;
        writeln!(fmt, "\tId:\t\t{}", self.id)?;
        writeln!(fmt, "\tAddress:\t{}", self.address.to_string())?;
        writeln!(fmt, "\tTechnology:\t{:?}", self.technology)?;
        if let Some(name) = &self.name {
            writeln!(fmt, "\tName:\t\t{}", name)?;
        }
        if let Some(appearance) = &self.appearance {
            writeln!(fmt, "\tAppearance:\t{:?}", appearance)?;
        }
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
            self.services
                .iter()
                .map(|uuid| {
                    let uuid = uuid.to_string();
                    find_service_uuid(&uuid).map(|an| an.name.to_string()).unwrap_or(uuid)
                })
                .collect::<Vec<_>>(),
        )?;
        Ok(())
    }
}

fn appearance_from_deprecated(src: fctrl::Appearance) -> Result<Option<Appearance>, Error> {
    Ok(match src {
        fctrl::Appearance::Unknown => None,
        src => Some(
            Appearance::from_primitive(src.into_primitive())
                .ok_or(format_err!("failed to construct appearance"))?,
        ),
    })
}

fn appearance_into_deprecated(src: Appearance) -> Result<fctrl::Appearance, Error> {
    fctrl::Appearance::from_primitive(src.into_primitive())
        .ok_or(format_err!("failed to construct appearance"))
}

fn address_from_deprecated(src: &str) -> Result<Address, Error> {
    // Since `RemoteDevice` does not encode the type of its address, that information is not available
    // to use here. We incorrectly encode all addresses as "public" instead of inventing a new
    // encoding because (a) the control API is deprecated and will be removed; (b) the existing Rust
    // users of the deprecated API do not use or display the type of an address.
    Address::public_from_str(src)
}

impl TryFrom<&fctrl::RemoteDevice> for Peer {
    type Error = Error;
    fn try_from(src: &fctrl::RemoteDevice) -> Result<Peer, Self::Error> {
        Ok(Peer {
            id: PeerId::from_str(&src.identifier)?,
            technology: match src.technology {
                fctrl::TechnologyType::LowEnergy => fsys::TechnologyType::LowEnergy,
                fctrl::TechnologyType::Classic => fsys::TechnologyType::Classic,
                fctrl::TechnologyType::DualMode => fsys::TechnologyType::DualMode,
            },
            address: address_from_deprecated(&src.address)?,
            connected: src.connected,
            bonded: src.bonded,
            name: src.name.clone(),
            appearance: appearance_from_deprecated(src.appearance.clone())?,
            device_class: None,
            rssi: src.rssi.as_ref().map(|rssi| rssi.value),
            tx_power: src.tx_power.as_ref().map(|tx| tx.value),
            services: src
                .service_uuids
                .iter()
                .map(|uuid| Uuid::from_str(uuid).map_err(|e| e.into()))
                .collect::<Result<Vec<Uuid>, Error>>()?,
        })
    }
}

impl TryFrom<fctrl::RemoteDevice> for Peer {
    type Error = Error;
    fn try_from(src: fctrl::RemoteDevice) -> Result<Peer, Self::Error> {
        Peer::try_from(&src)
    }
}

impl From<&Peer> for fctrl::RemoteDevice {
    fn from(src: &Peer) -> fctrl::RemoteDevice {
        fctrl::RemoteDevice {
            identifier: src.id.to_string(),
            address: src.address.to_string(),
            technology: match src.technology {
                fsys::TechnologyType::LowEnergy => fctrl::TechnologyType::LowEnergy,
                fsys::TechnologyType::Classic => fctrl::TechnologyType::Classic,
                fsys::TechnologyType::DualMode => fctrl::TechnologyType::DualMode,
            },
            name: src.name.clone(),
            appearance: src
                .appearance
                .map(|a| appearance_into_deprecated(a))
                .map_or(Ok(fctrl::Appearance::Unknown), |a| a)
                .unwrap_or(fctrl::Appearance::Unknown),
            rssi: src.rssi.map(|r| Box::new(fidl_fuchsia_bluetooth::Int8 { value: r })),
            tx_power: src.tx_power.map(|t| Box::new(fidl_fuchsia_bluetooth::Int8 { value: t })),
            connected: src.connected,
            bonded: src.bonded,
            service_uuids: src.services.iter().map(|uuid| uuid.to_string()).collect(),
        }
    }
}

impl From<Peer> for fctrl::RemoteDevice {
    fn from(src: Peer) -> fctrl::RemoteDevice {
        fctrl::RemoteDevice::from(&src)
    }
}

impl TryFrom<fsys::Peer> for Peer {
    type Error = Error;
    fn try_from(src: fsys::Peer) -> Result<Peer, Self::Error> {
        Ok(Peer {
            id: src.id.ok_or(format_err!("`Peer.id` is mandatory"))?.into(),
            address: src.address.ok_or(format_err!("`Peer.address` is mandatory"))?.into(),
            technology: src.technology.ok_or(format_err!("`Peer.technology` is mandatory!"))?,
            connected: src.connected.unwrap_or(false),
            bonded: src.bonded.unwrap_or(false),
            name: src.name.clone(),
            appearance: src.appearance,
            device_class: src.device_class,
            rssi: src.rssi,
            tx_power: src.tx_power,
            services: src.services.unwrap_or(vec![]).iter().map(Uuid::from).collect(),
            // TODO(fxbug.dev/59628): Convert le_services and bredr_services fields.
        })
    }
}

impl From<&Peer> for fsys::Peer {
    fn from(src: &Peer) -> fsys::Peer {
        fsys::Peer {
            id: Some(src.id.into()),
            address: Some(src.address.into()),
            technology: Some(src.technology),
            connected: Some(src.connected),
            bonded: Some(src.bonded),
            name: src.name.clone(),
            appearance: src.appearance,
            device_class: src.device_class,
            rssi: src.rssi,
            tx_power: src.tx_power,
            services: Some(src.services.iter().map(|uuid| uuid.into()).collect()),

            // TODO(fxbug.dev/59628): Convert le_services and bredr_services fields.
            le_services: None,
            bredr_services: None,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_bluetooth as fbt,
        proptest::{collection::vec, option, prelude::*},
    };

    #[test]
    fn try_from_control_invalid_id() {
        let peer = fctrl::RemoteDevice {
            identifier: "💩".to_string(),
            technology: fctrl::TechnologyType::DualMode,
            address: "public_00:01:02:03:04:05".to_string(),
            name: None,
            appearance: fctrl::Appearance::Unknown,
            rssi: None,
            tx_power: None,
            connected: false,
            bonded: false,
            service_uuids: vec![],
        };
        let peer = Peer::try_from(&peer);
        assert!(peer.is_err());
    }

    #[test]
    fn try_from_control_invalid_address() {
        let peer = fctrl::RemoteDevice {
            identifier: "1".to_string(),
            technology: fctrl::TechnologyType::DualMode,
            address: "💩".to_string(),
            name: None,
            appearance: fctrl::Appearance::Unknown,
            rssi: None,
            tx_power: None,
            connected: false,
            bonded: false,
            service_uuids: vec![],
        };
        let peer = Peer::try_from(&peer);
        assert!(peer.is_err());
    }

    #[test]
    fn try_from_sys_id_not_present() {
        let peer = fsys::Peer::empty();
        let peer = Peer::try_from(peer);
        assert!(peer.is_err());
    }

    #[test]
    fn try_from_sys_address_not_present() {
        let peer = fsys::Peer { id: Some(fbt::PeerId { value: 1 }), ..fsys::Peer::empty() };
        let peer = Peer::try_from(peer);
        assert!(peer.is_err());
    }

    #[test]
    fn try_from_sys_technology_not_present() {
        let peer = fsys::Peer {
            id: Some(fbt::PeerId { value: 1 }),
            address: Some(fbt::Address {
                type_: fbt::AddressType::Public,
                bytes: [1, 2, 3, 4, 5, 6],
            }),
            ..fsys::Peer::empty()
        };
        let peer = Peer::try_from(peer);
        assert!(peer.is_err());
    }

    fn any_address() -> impl Strategy<Value = Address> {
        any::<[u8; 6]>().prop_map(Address::Public)
    }

    fn any_technology() -> impl Strategy<Value = fsys::TechnologyType> {
        prop_oneof![
            Just(fsys::TechnologyType::LowEnergy),
            Just(fsys::TechnologyType::Classic),
            Just(fsys::TechnologyType::DualMode),
        ]
    }

    fn any_appearance() -> impl Strategy<Value = Appearance> {
        prop_oneof![Just(Appearance::Unknown), Just(Appearance::Computer),]
    }

    fn any_device_class() -> impl Strategy<Value = DeviceClass> {
        any::<u32>().prop_map(|value| DeviceClass { value })
    }

    fn any_uuids() -> impl Strategy<Value = Vec<Uuid>> {
        vec(any::<[u8; 16]>().prop_map(Uuid::from_bytes), 0..3)
    }

    fn any_peer() -> impl Strategy<Value = Peer> {
        (
            // Trait `Strategy` is only implemented for tuples of up to size 10.
            (
                any::<u64>(), // id
                any_address(),
                any_technology(),
                any::<bool>(),                       // connected
                any::<bool>(),                       // bonded
                option::of("[a-zA-Z][a-zA-Z0-9_]*"), // name
                option::of(any_appearance()),
                option::of(any_device_class()),
                option::of(any::<i8>()), // rssi
                option::of(any::<i8>()), // tx power
            ),
            any_uuids(), // services
        )
            .prop_map(
                |(
                    (
                        id,
                        address,
                        technology,
                        connected,
                        bonded,
                        name,
                        appearance,
                        device_class,
                        rssi,
                        tx_power,
                    ),
                    services,
                )| {
                    Peer {
                        id: PeerId(id),
                        address,
                        technology,
                        connected,
                        bonded,
                        name,
                        appearance,
                        device_class,
                        rssi,
                        tx_power,
                        services,
                    }
                },
            )
    }

    proptest! {
        #[test]
        fn peer_control_roundtrip(mut peer in any_peer()) {
            use std::convert::TryInto;

            // The `appearance` field of `RemoteDevice` is not optional and a missing value from
            // `Peer` is represented on conversion as `Appearance::Unknown`. However a valid value
            // of `Appearance::Unknown` is also represented the same way. Since these do not map
            // well in a round-trip conversion test, we align this to the target conversion state.
            peer.appearance = peer.appearance
                .map(|a| if a == Appearance::Unknown { None } else { Some(a) })
                .unwrap_or(None);

            // `RemoteDevice` does not have a DeviceClass field and converting one into a `Peer`
            // always initializes this field to None.
            peer.device_class = None;

            let control = fctrl::RemoteDevice::from(&peer);
            assert_eq!(Ok(peer), control.try_into().map_err(|e: anyhow::Error| e.to_string()));
        }

        fn peer_sys_roundtrip(peer in any_peer()) {
            use std::convert::TryInto;

            let sys = fsys::Peer::from(&peer);
            assert_eq!(Ok(peer), sys.try_into().map_err(|e: anyhow::Error| e.to_string()));
        }
    }
}
