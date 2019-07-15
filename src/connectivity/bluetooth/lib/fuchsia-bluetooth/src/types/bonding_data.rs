// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        inspect::{DebugExt, InspectData, Inspectable, IsInspectable, ToProperty},
        util,
    },
    fidl_fuchsia_bluetooth_control as control, fuchsia_inspect as inspect,
    std::ops,
};

#[derive(Debug)]
struct LeInspect {
    _inspect: inspect::Node,
    _address_type: inspect::StringProperty,
    _services: inspect::StringProperty,
    _connection_interval: Option<inspect::UintProperty>,
    _connection_latency: Option<inspect::UintProperty>,
    _supervision_timeout: Option<inspect::UintProperty>,
    _ltk_authenticated: inspect::UintProperty,
    _ltk_secure_connections: inspect::UintProperty,
    _ltk_encryption_key_size: Option<inspect::UintProperty>,
    _irk_authenticated: inspect::UintProperty,
    _irk_secure_connections: inspect::UintProperty,
    _irk_encryption_key_size: Option<inspect::UintProperty>,
    _csrk_authenticated: inspect::UintProperty,
    _csrk_secure_connections: inspect::UintProperty,
    _csrk_encryption_key_size: Option<inspect::UintProperty>,
}

impl LeInspect {
    fn new(d: &control::LeData, inspect: inspect::Node) -> LeInspect {
        LeInspect {
            _address_type: inspect.create_string("address_type", d.address_type.debug()),
            _services: inspect.create_string("services", d.services.to_property()),

            _connection_interval: d
                .connection_parameters
                .as_ref()
                .map(|p| inspect.create_uint("connection_interval", p.connection_interval as u64)),
            _connection_latency: d
                .connection_parameters
                .as_ref()
                .map(|p| inspect.create_uint("connection_latency", p.connection_latency as u64)),
            _supervision_timeout: d
                .connection_parameters
                .as_ref()
                .map(|p| inspect.create_uint("supervision_timeout", p.supervision_timeout as u64)),

            _ltk_authenticated: inspect.create_uint(
                "ltk_authenticated",
                d.ltk.as_ref().map(|ltk| ltk.key.security_properties.authenticated).to_property(),
            ),
            _ltk_secure_connections: inspect.create_uint(
                "ltk_secure_connections",
                d.ltk
                    .as_ref()
                    .map(|ltk| ltk.key.security_properties.secure_connections)
                    .to_property(),
            ),
            _ltk_encryption_key_size: d.ltk.as_ref().map(|ltk| {
                inspect.create_uint(
                    "ltk_encryption_key_size",
                    ltk.key.security_properties.encryption_key_size as u64,
                )
            }),

            _irk_authenticated: inspect.create_uint(
                "irk_authenticated",
                d.irk.as_ref().map(|k| k.security_properties.authenticated).to_property(),
            ),
            _irk_secure_connections: inspect.create_uint(
                "irk_secure_connections",
                d.irk.as_ref().map(|k| k.security_properties.secure_connections).to_property(),
            ),
            _irk_encryption_key_size: d.irk.as_ref().map(|k| {
                inspect.create_uint(
                    "irk_encryption_key_size",
                    k.security_properties.encryption_key_size as u64,
                )
            }),

            _csrk_authenticated: inspect.create_uint(
                "csrk_authenticated",
                d.csrk.as_ref().map(|k| k.security_properties.authenticated).to_property(),
            ),
            _csrk_secure_connections: inspect.create_uint(
                "csrk_secure_connections",
                d.csrk.as_ref().map(|k| k.security_properties.secure_connections).to_property(),
            ),
            _csrk_encryption_key_size: d.csrk.as_ref().map(|k| {
                inspect.create_uint(
                    "csrk_encryption_key_size",
                    k.security_properties.encryption_key_size as u64,
                )
            }),

            _inspect: inspect,
        }
    }
}

#[derive(Debug)]
struct BredrInspect {
    _inspect: inspect::Node,
    _piconet_leader: inspect::UintProperty,
    _services: inspect::StringProperty,
    _lk_authenticated: inspect::UintProperty,
    _lk_secure_connections: inspect::UintProperty,
    _lk_encryption_key_size: Option<inspect::UintProperty>,
}

impl BredrInspect {
    fn new(d: &control::BredrData, inspect: inspect::Node) -> BredrInspect {
        BredrInspect {
            _piconet_leader: inspect.create_uint("piconet_leader", d.piconet_leader.to_property()),
            _services: inspect.create_string("services", d.services.to_property()),
            _lk_authenticated: inspect.create_uint(
                "authenticated",
                d.link_key
                    .as_ref()
                    .map(|ltk| ltk.key.security_properties.authenticated)
                    .to_property(),
            ),
            _lk_secure_connections: inspect.create_uint(
                "secure_connections",
                d.link_key
                    .as_ref()
                    .map(|ltk| ltk.key.security_properties.secure_connections)
                    .to_property(),
            ),
            _lk_encryption_key_size: d.link_key.as_ref().map(|ltk| {
                inspect.create_uint(
                    "encryption_key_size",
                    ltk.key.security_properties.encryption_key_size as u64,
                )
            }),
            _inspect: inspect,
        }
    }
}

#[derive(Debug)]
pub struct BondingDataInspect {
    _inspect: inspect::Node,
    _le_inspect: Option<LeInspect>,
    _bredr_inspect: Option<BredrInspect>,
}

impl InspectData<BondingData> for BondingDataInspect {
    fn new(bd: &BondingData, inspect: inspect::Node) -> BondingDataInspect {
        BondingDataInspect {
            _le_inspect: bd.le.as_ref().map(|d| LeInspect::new(d, inspect.create_child("le"))),
            _bredr_inspect: bd
                .bredr
                .as_ref()
                .map(|d| BredrInspect::new(d, inspect.create_child("bredr"))),
            _inspect: inspect,
        }
    }
}

impl IsInspectable for BondingData {
    type I = BondingDataInspect;
}

#[derive(Clone, Debug, PartialEq)]
pub struct BondingData {
    pub identifier: String,
    pub local_address: String,
    pub name: Option<String>,
    pub le: Option<control::LeData>,
    pub bredr: Option<control::BredrData>,
}

impl From<control::BondingData> for BondingData {
    fn from(bd: control::BondingData) -> BondingData {
        BondingData {
            identifier: bd.identifier,
            local_address: bd.local_address,
            name: bd.name,
            le: bd.le.map(|le| *le),
            bredr: bd.bredr.map(|bredr| *bredr),
        }
    }
}

impl From<BondingData> for control::BondingData {
    fn from(bd: BondingData) -> control::BondingData {
        control::BondingData {
            identifier: bd.identifier,
            local_address: bd.local_address,
            name: bd.name,
            le: bd.le.map(|le| Box::new(le)),
            bredr: bd.bredr.map(|bredr| Box::new(bredr)),
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_bluetooth_control as control};

    fn new_bond() -> BondingData {
        let remote_key = control::RemoteKey {
            security_properties: control::SecurityProperties {
                authenticated: false,
                secure_connections: true,
                encryption_key_size: 0,
            },
            value: [0; 16],
        };

        let ltk = control::Ltk { key: remote_key.clone(), key_size: 0, ediv: 1, rand: 2 };

        BondingData {
            identifier: "id".into(),
            local_address: "addr".into(),
            name: Some("name".into()),
            le: Some(control::LeData {
                address: "le_addr".into(),
                address_type: control::AddressType::LeRandom,
                connection_parameters: Some(Box::new(control::LeConnectionParameters {
                    connection_interval: 0,
                    connection_latency: 1,
                    supervision_timeout: 2,
                })),
                services: vec![],
                ltk: Some(Box::new(ltk.clone())),
                irk: Some(Box::new(remote_key.clone())),
                csrk: Some(Box::new(remote_key.clone())),
            }),
            bredr: Some(control::BredrData {
                address: "bredr_addr".into(),
                piconet_leader: true,
                services: vec![],
                link_key: Some(Box::new(ltk.clone())),
            }),
        }
    }

    #[test]
    fn bonding_data_conversion() {
        let bond = new_bond();
        assert_eq!(bond, BondingData::from(control::BondingData::from(bond.clone())));
    }
}
