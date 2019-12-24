// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module declares native Rust encodings equivalent to FIDL structs for the
//! Bluetooth LowEnergy interfaces. These structs use standard Rust primitives
//! rather than the default mapping from FIDL, and derive the `Clone` trait for a
//! more ergonomic api than those exposed in the `fidl_fuchsia_bluetooth_le`
//! crate.
//!
//! These types also implement the `From` trait, so usage when receiving a fidl
//! struct is simply a case of calling `.into(...)` (`From` implies `Into`):
//!
//! ```ignore
//!   fn use_peer(fidl_peer: fidl_fuchsia_bluetooth_le::RemoteDevice) {
//!      let peer: Le::RemoteDevice = fidl_peer.into();
//!      ...
//!   }
//! ```

use crate::types::{id::PeerId, uuid::Uuid};
use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth::Appearance,
    fidl_fuchsia_bluetooth_le as fidl,
    std::{
        convert::{TryFrom, TryInto},
        fmt,
        str::FromStr,
    },
};

#[derive(Clone)]
pub struct RemoteDevice {
    pub identifier: String,
    pub connectable: bool,
    pub rssi: Option<i8>,
    pub advertising_data: Option<AdvertisingData>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Peer {
    pub id: PeerId,
    pub connectable: bool,
    pub rssi: Option<i8>,
    pub advertising_data: Option<AdvertisingData>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct AdvertisingData {
    pub name: Option<String>,
    pub tx_power_level: Option<i8>,
    pub appearance: Option<Appearance>,
    pub service_uuids: Vec<Uuid>,
    pub service_data: Vec<ServiceData>,
    pub manufacturer_data: Vec<ManufacturerData>,
    pub uris: Vec<String>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct ServiceData {
    pub uuid: Uuid,
    pub data: Vec<u8>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct ManufacturerData {
    pub company_id: u16,
    pub data: Vec<u8>,
}

impl TryFrom<fidl::RemoteDevice> for RemoteDevice {
    type Error = Error;
    fn try_from(src: fidl::RemoteDevice) -> Result<RemoteDevice, Self::Error> {
        Ok(RemoteDevice {
            identifier: src.identifier,
            connectable: src.connectable,
            rssi: src.rssi.map(|v| v.value),
            advertising_data: match src.advertising_data {
                Some(a) => Some(AdvertisingData::try_from(*a)?),
                None => None,
            },
        })
    }
}

impl TryFrom<fidl::Peer> for Peer {
    type Error = Error;
    fn try_from(src: fidl::Peer) -> Result<Peer, Error> {
        Ok(Peer {
            id: src
                .id
                .ok_or(format_err!("`le.Peer` missing mandatory `id` field"))
                .map(PeerId::from)?,
            connectable: src.connectable.unwrap_or(false),
            rssi: src.rssi,
            advertising_data: src.advertising_data.map(|ad| ad.into()),
        })
    }
}

impl From<fidl::AdvertisingData> for AdvertisingData {
    fn from(src: fidl::AdvertisingData) -> AdvertisingData {
        AdvertisingData {
            name: src.name,
            tx_power_level: src.tx_power_level,
            appearance: src.appearance,
            service_uuids: src
                .service_uuids
                .unwrap_or(vec![])
                .into_iter()
                .map(|uuid| Uuid::from(uuid))
                .collect(),
            service_data: src
                .service_data
                .unwrap_or(vec![])
                .into_iter()
                .map(|data| data.into())
                .collect(),
            manufacturer_data: src
                .manufacturer_data
                .unwrap_or(vec![])
                .into_iter()
                .map(|data| data.into())
                .collect(),
            uris: src.uris.unwrap_or(vec![]),
        }
    }
}

impl TryFrom<fidl::AdvertisingDataDeprecated> for AdvertisingData {
    type Error = Error;
    fn try_from(src: fidl::AdvertisingDataDeprecated) -> Result<AdvertisingData, Self::Error> {
        Ok(AdvertisingData {
            name: src.name,
            tx_power_level: src.tx_power_level.map(|v| v.value),
            appearance: src
                .appearance
                .map(|v| {
                    Appearance::from_primitive(v.value).ok_or(format_err!("invalid appearance"))
                })
                .map_or(Ok(None), |v| v.map(Some))?,
            service_uuids: src
                .service_uuids
                .unwrap_or(vec![])
                .into_iter()
                .map(|uuid| Uuid::from_str(&uuid).map_err(|e| e.into()))
                .collect::<Result<Vec<Uuid>, Error>>()?,
            service_data: src
                .service_data
                .unwrap_or(vec![])
                .into_iter()
                .map(ServiceData::try_from)
                .collect::<Result<Vec<_>, Error>>()?,
            manufacturer_data: src
                .manufacturer_specific_data
                .unwrap_or(vec![])
                .into_iter()
                .map(|data| data.into())
                .collect(),
            uris: src.uris.unwrap_or(vec![]),
        })
    }
}

impl TryFrom<fidl::ServiceDataEntry> for ServiceData {
    type Error = Error;
    fn try_from(src: fidl::ServiceDataEntry) -> Result<ServiceData, Self::Error> {
        Ok(ServiceData { uuid: Uuid::from_str(&src.uuid)?, data: src.data })
    }
}

impl From<fidl::ServiceData> for ServiceData {
    fn from(src: fidl::ServiceData) -> ServiceData {
        ServiceData { uuid: Uuid::from(src.uuid), data: src.data }
    }
}

impl From<fidl::ManufacturerSpecificDataEntry> for ManufacturerData {
    fn from(src: fidl::ManufacturerSpecificDataEntry) -> ManufacturerData {
        ManufacturerData { company_id: src.company_id, data: src.data }
    }
}

impl From<fidl::ManufacturerData> for ManufacturerData {
    fn from(src: fidl::ManufacturerData) -> ManufacturerData {
        ManufacturerData { company_id: src.company_id, data: src.data }
    }
}

impl fmt::Display for RemoteDevice {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let connectable = if self.connectable { "connectable" } else { "non-connectable" };

        write!(f, "[device ({}) ", connectable)?;

        if let Some(rssi) = &self.rssi {
            write!(f, "rssi: {}, ", rssi)?;
        }

        if let Some(ad) = &self.advertising_data {
            if let Some(name) = &ad.name {
                write!(f, "{}, ", name)?;
            }
        }

        write!(f, "id: {}]", self.identifier)
    }
}

impl fmt::Display for Peer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let connectable = if self.connectable { "connectable" } else { "non-connectable" };

        write!(f, "[peer ({}) ", connectable)?;

        if let Some(rssi) = &self.rssi {
            write!(f, "rssi: {}, ", rssi)?;
        }

        if let Some(ad) = &self.advertising_data {
            if let Some(name) = &ad.name {
                write!(f, "{}, ", name)?;
            }
        }

        write!(f, "id: {}]", self.id)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_bluetooth as fbt;
    use fidl_fuchsia_bluetooth_le as fble;

    #[test]
    fn advertising_data_from_fidl_empty() {
        let data = fble::AdvertisingData {
            name: None,
            tx_power_level: None,
            appearance: None,
            service_uuids: None,
            service_data: None,
            manufacturer_data: None,
            uris: None,
        };
        let expected = AdvertisingData {
            name: None,
            tx_power_level: None,
            appearance: None,
            service_uuids: vec![],
            service_data: vec![],
            manufacturer_data: vec![],
            uris: vec![],
        };
        let data = AdvertisingData::from(data);
        assert_eq!(expected, data);
    }

    #[test]
    fn advertising_data_from_fidl() {
        let uuid = fbt::Uuid {
            value: [
                0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x0d, 0x18,
                0x00, 0x00,
            ],
        };
        let data = fble::AdvertisingData {
            name: Some("hello".to_string()),
            tx_power_level: Some(-10),
            appearance: Some(fbt::Appearance::Watch),
            service_uuids: Some(vec![uuid.clone()]),
            service_data: Some(vec![fble::ServiceData { uuid: uuid.clone(), data: vec![1, 2, 3] }]),
            manufacturer_data: Some(vec![fble::ManufacturerData {
                company_id: 1,
                data: vec![3, 4, 5],
            }]),
            uris: Some(vec!["some/uri".to_string()]),
        };
        let expected = AdvertisingData {
            name: Some("hello".to_string()),
            tx_power_level: Some(-10),
            appearance: Some(fbt::Appearance::Watch),
            service_uuids: vec![Uuid::new16(0x180d)],
            service_data: vec![ServiceData { uuid: Uuid::new16(0x180d), data: vec![1, 2, 3] }],
            manufacturer_data: vec![ManufacturerData { company_id: 1, data: vec![3, 4, 5] }],
            uris: vec!["some/uri".to_string()],
        };
        let data = AdvertisingData::from(data);
        assert_eq!(expected, data);
    }

    #[test]
    fn advertising_data_from_deprecated_fidl_malformed_appearance() {
        let data = fble::AdvertisingDataDeprecated {
            name: None,
            tx_power_level: None,
            appearance: Some(Box::new(fbt::UInt16 { value: 1 })), // fbt::Appearance does not declare this entry
            service_uuids: None,
            service_data: None,
            manufacturer_specific_data: None,
            solicited_service_uuids: None,
            uris: None,
        };
        let data = AdvertisingData::try_from(data);
        assert!(data.is_err());
    }

    #[test]
    fn advertising_data_from_deprecated_fidl_malformed_service_uuid() {
        let data = fble::AdvertisingDataDeprecated {
            name: None,
            tx_power_level: None,
            appearance: None,
            service_uuids: Some(vec!["💩".to_string()]),
            service_data: None,
            manufacturer_specific_data: None,
            solicited_service_uuids: None,
            uris: None,
        };
        let data = AdvertisingData::try_from(data);
        assert!(data.is_err());
    }

    #[test]
    fn advertising_data_from_deprecated_fidl_malformed_service_data() {
        let data = fble::AdvertisingDataDeprecated {
            name: None,
            tx_power_level: None,
            appearance: None,
            service_uuids: None,
            service_data: Some(vec![fble::ServiceDataEntry {
                uuid: "💩".to_string(),
                data: vec![1, 2],
            }]),
            manufacturer_specific_data: None,
            solicited_service_uuids: None,
            uris: None,
        };
        let data = AdvertisingData::try_from(data);
        assert!(data.is_err());
    }

    #[test]
    fn advertising_data_from_deprecated_fidl() {
        let data = fble::AdvertisingDataDeprecated {
            name: Some("hello".to_string()),
            tx_power_level: Some(Box::new(fbt::Int8 { value: -10 })),
            appearance: Some(Box::new(fbt::UInt16 { value: 64 })), // "Phone"
            service_uuids: Some(vec!["0000180d-0000-1000-8000-00805f9b34fb".to_string()]),
            service_data: Some(vec![fble::ServiceDataEntry {
                uuid: "0000180d-0000-1000-8000-00805f9b34fb".to_string(),
                data: vec![1, 2],
            }]),
            manufacturer_specific_data: Some(vec![fble::ManufacturerSpecificDataEntry {
                company_id: 1,
                data: vec![1],
            }]),
            solicited_service_uuids: None,
            uris: Some(vec!["some/uri".to_string()]),
        };
        let expected = AdvertisingData {
            name: Some("hello".to_string()),
            tx_power_level: Some(-10),
            appearance: Some(fbt::Appearance::Phone),
            service_uuids: vec![Uuid::new16(0x180d)],
            service_data: vec![ServiceData { uuid: Uuid::new16(0x180d), data: vec![1, 2] }],
            manufacturer_data: vec![ManufacturerData { company_id: 1, data: vec![1] }],
            uris: vec!["some/uri".to_string()],
        };
        let data = AdvertisingData::try_from(data).expect("expected successful conversion");
        assert_eq!(expected, data);
    }

    #[test]
    fn peer_from_fidl_no_id() {
        let peer = fble::Peer { id: None, connectable: None, rssi: None, advertising_data: None };
        let peer = Peer::try_from(peer);
        assert!(peer.is_err());
    }

    #[test]
    fn peer_from_fidl_mandatory_fields_only() {
        let peer = fble::Peer {
            id: Some(fbt::PeerId { value: 1 }),
            connectable: None,
            rssi: None,
            advertising_data: None,
        };
        let expected =
            Peer { id: PeerId(1), connectable: false, rssi: None, advertising_data: None };
        let peer = Peer::try_from(peer).expect("expected successful conversion");
        assert_eq!(expected, peer);
    }

    #[test]
    fn peer_from_fidl() {
        let peer = fble::Peer {
            id: Some(fbt::PeerId { value: 1 }),
            connectable: Some(true),
            rssi: Some(-10),
            advertising_data: Some(fble::AdvertisingData {
                name: Some("hello".to_string()),
                tx_power_level: Some(-10),
                appearance: Some(fbt::Appearance::Watch),
                service_uuids: None,
                service_data: None,
                manufacturer_data: None,
                uris: None,
            }),
        };
        let expected = Peer {
            id: PeerId(1),
            connectable: true,
            rssi: Some(-10),
            advertising_data: Some(AdvertisingData {
                name: Some("hello".to_string()),
                tx_power_level: Some(-10),
                appearance: Some(fbt::Appearance::Watch),
                service_uuids: vec![],
                service_data: vec![],
                manufacturer_data: vec![],
                uris: vec![],
            }),
        };
        let peer = Peer::try_from(peer).expect("expected successful conversion");
        assert_eq!(expected, peer);
    }
}
