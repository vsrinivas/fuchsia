// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl_fuchsia_bluetooth as bt, fidl_fuchsia_bluetooth_control as control,
    fidl_fuchsia_bluetooth_sys as sys, fuchsia_inspect as inspect,
    std::{
        convert::{TryFrom, TryInto},
        ops,
    },
};

use crate::{
    inspect::{DebugExt, InspectData, Inspectable, IsInspectable, ToProperty},
    types::{uuid::Uuid, Address, OneOrBoth, PeerId},
    util::{self, CollectExt},
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
    fn new(d: &LeData, inspect: inspect::Node) -> LeInspect {
        LeInspect {
            _address_type: inspect.create_string("address_type", d.address.address_type_string()),
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
                d.ltk.as_ref().map(|ltk| ltk.key.security.authenticated).to_property(),
            ),
            _ltk_secure_connections: inspect.create_uint(
                "ltk_secure_connections",
                d.ltk.as_ref().map(|ltk| ltk.key.security.secure_connections).to_property(),
            ),
            _ltk_encryption_key_size: d.ltk.as_ref().map(|ltk| {
                inspect.create_uint(
                    "ltk_encryption_key_size",
                    ltk.key.security.encryption_key_size as u64,
                )
            }),

            _irk_authenticated: inspect.create_uint(
                "irk_authenticated",
                d.irk.as_ref().map(|k| k.security.authenticated).to_property(),
            ),
            _irk_secure_connections: inspect.create_uint(
                "irk_secure_connections",
                d.irk.as_ref().map(|k| k.security.secure_connections).to_property(),
            ),
            _irk_encryption_key_size: d.irk.as_ref().map(|irk| {
                inspect
                    .create_uint("irk_encryption_key_size", irk.security.encryption_key_size as u64)
            }),
            _csrk_authenticated: inspect.create_uint(
                "csrk_authenticated",
                d.csrk.as_ref().map(|k| k.security.authenticated).to_property(),
            ),
            _csrk_secure_connections: inspect.create_uint(
                "csrk_secure_connections",
                d.csrk.as_ref().map(|k| k.security.secure_connections).to_property(),
            ),
            _csrk_encryption_key_size: d.csrk.as_ref().map(|k| {
                inspect
                    .create_uint("csrk_encryption_key_size", k.security.encryption_key_size as u64)
            }),

            _inspect: inspect,
        }
    }
}

#[derive(Debug)]
struct BredrInspect {
    _inspect: inspect::Node,
    _role_preference: Option<inspect::StringProperty>,
    _services: inspect::StringProperty,
    _lk_authenticated: inspect::UintProperty,
    _lk_secure_connections: inspect::UintProperty,
    _lk_encryption_key_size: Option<inspect::UintProperty>,
}

impl BredrInspect {
    fn new(d: &BredrData, inspect: inspect::Node) -> BredrInspect {
        BredrInspect {
            _role_preference: d.role_preference.as_ref().map(|role| {
                inspect.create_string(
                    "role_preference",
                    match role {
                        bt::ConnectionRole::Leader => "Leader",
                        bt::ConnectionRole::Follower => "Follower",
                    },
                )
            }),
            _services: inspect.create_string("services", d.services.to_property()),
            _lk_authenticated: inspect.create_uint(
                "authenticated",
                d.link_key.as_ref().map(|ltk| ltk.security.authenticated).to_property(),
            ),
            _lk_secure_connections: inspect.create_uint(
                "secure_connections",
                d.link_key.as_ref().map(|ltk| ltk.security.secure_connections).to_property(),
            ),
            _lk_encryption_key_size: d.link_key.as_ref().map(|ltk| {
                inspect.create_uint("encryption_key_size", ltk.security.encryption_key_size as u64)
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
            _le_inspect: bd.le().map(|d| LeInspect::new(d, inspect.create_child("le"))),
            _bredr_inspect: bd.bredr().map(|d| BredrInspect::new(d, inspect.create_child("bredr"))),
            _inspect: inspect,
        }
    }
}

impl IsInspectable for BondingData {
    type I = BondingDataInspect;
}

/// Bluetooth Low Energy specific bonding data
#[derive(Clone, Debug, PartialEq)]
pub struct LeData {
    /// The identity address of the peer.
    pub address: Address,
    /// The peer's preferred connection parameters, if known.
    pub connection_parameters: Option<sys::LeConnectionParameters>,
    /// Known GATT service UUIDs.
    pub services: Vec<Uuid>,
    /// The LE long-term key. Present if the link was encrypted.
    pub ltk: Option<sys::Ltk>,
    /// Identity Resolving RemoteKey used to generate and resolve random addresses.
    pub irk: Option<sys::PeerKey>,
    /// Connection Signature Resolving RemoteKey used for data signing without encryption.
    pub csrk: Option<sys::PeerKey>,
}
/// Bluetooth BR/EDR (Classic) specific bonding data
#[derive(Clone, Debug, PartialEq)]
pub struct BredrData {
    /// The public device address of the peer.
    pub address: Address,
    /// True if the peer prefers to lead the piconet. This is determined by role switch procedures.
    /// Paging and connecting from a peer does not automatically set this flag.
    pub role_preference: Option<bt::ConnectionRole>,
    /// Known service UUIDs obtained from EIR data or SDP.
    pub services: Vec<Uuid>,
    /// The semi-permanent BR/EDR key. Present if link was paired with Secure Simple Pairing or
    /// stronger.
    pub link_key: Option<sys::PeerKey>,
}

/// TODO(36378) - Compatibility functions to convert from the to-be-deprecated Control api types
/// into the newer sys types. To be removed when the Control library is removed.
mod compat {
    use fidl_fuchsia_bluetooth_control as control;
    use fidl_fuchsia_bluetooth_sys as sys;

    pub fn peer_key_from_control(src: control::RemoteKey) -> sys::PeerKey {
        sys::PeerKey {
            security: sys_security_from_control(src.security_properties),
            data: sys::Key { value: src.value },
        }
    }

    pub fn ltk_from_control(src: control::Ltk) -> sys::Ltk {
        sys::Ltk { key: peer_key_from_control(src.key), ediv: src.ediv, rand: src.rand }
    }

    pub fn peer_key_to_control(src: sys::PeerKey) -> control::RemoteKey {
        control::RemoteKey {
            security_properties: sys_security_to_control(src.security),
            value: src.data.value,
        }
    }

    pub fn ltk_to_control(src: sys::Ltk) -> control::Ltk {
        control::Ltk {
            key: peer_key_to_control(src.key),
            ediv: src.ediv,
            rand: src.rand,
            key_size: src.key.security.encryption_key_size,
        }
    }

    pub fn sys_security_from_control(src: control::SecurityProperties) -> sys::SecurityProperties {
        sys::SecurityProperties {
            authenticated: src.authenticated,
            secure_connections: src.secure_connections,
            encryption_key_size: src.encryption_key_size,
        }
    }

    pub fn sys_security_to_control(src: sys::SecurityProperties) -> control::SecurityProperties {
        control::SecurityProperties {
            authenticated: src.authenticated,
            secure_connections: src.secure_connections,
            encryption_key_size: src.encryption_key_size,
        }
    }

    pub fn sys_conn_params_from_control(
        src: control::LeConnectionParameters,
    ) -> sys::LeConnectionParameters {
        sys::LeConnectionParameters {
            connection_interval: src.connection_interval,
            connection_latency: src.connection_latency,
            supervision_timeout: src.supervision_timeout,
        }
    }
    pub fn sys_conn_params_to_control(
        src: sys::LeConnectionParameters,
    ) -> control::LeConnectionParameters {
        control::LeConnectionParameters {
            connection_interval: src.connection_interval,
            connection_latency: src.connection_latency,
            supervision_timeout: src.supervision_timeout,
        }
    }
}

impl TryFrom<control::LeData> for LeData {
    type Error = anyhow::Error;
    fn try_from(src: control::LeData) -> Result<LeData, Self::Error> {
        let address = match src.address_type {
            LE_PUBLIC => Address::public_from_str(&src.address)?,
            LE_RANDOM => Address::random_from_str(&src.address)?,
            _ => return Err(format_err!("Invalid address type, expected LE_PUBLIC or LE_RANDOM")),
        };
        let services: Result<Vec<Uuid>, uuid::parser::ParseError> =
            src.services.iter().map(|s| s.parse::<Uuid>()).collect();
        Ok(LeData {
            address,
            connection_parameters: src
                .connection_parameters
                .map(|params| compat::sys_conn_params_from_control(*params)),
            services: services?,
            ltk: src.ltk.map(|ltk| compat::ltk_from_control(*ltk)),
            irk: src.irk.map(|irk| compat::peer_key_from_control(*irk)),
            csrk: src.csrk.map(|csrk| compat::peer_key_from_control(*csrk)),
        })
    }
}
impl TryFrom<sys::LeData> for LeData {
    type Error = anyhow::Error;
    fn try_from(src: sys::LeData) -> Result<LeData, Self::Error> {
        Ok(LeData {
            address: src.address.ok_or(format_err!("No address"))?.into(),
            connection_parameters: src.connection_parameters,
            services: src.services.unwrap_or(vec![]).iter().map(|uuid| uuid.into()).collect(),
            ltk: src.ltk,
            irk: src.irk,
            csrk: src.csrk,
        })
    }
}

impl TryFrom<control::BredrData> for BredrData {
    type Error = anyhow::Error;
    fn try_from(src: control::BredrData) -> Result<BredrData, Self::Error> {
        let address = Address::public_from_str(&src.address)?;
        let role_preference = Some(if src.piconet_leader {
            bt::ConnectionRole::Leader
        } else {
            bt::ConnectionRole::Follower
        });
        let services = src.services.iter().map(|uuid_str| uuid_str.parse()).collect_results()?;
        let link_key = src.link_key.map(|ltk| compat::peer_key_from_control(ltk.key));
        Ok(BredrData { address, role_preference, services, link_key })
    }
}
impl TryFrom<sys::BredrData> for BredrData {
    type Error = anyhow::Error;
    fn try_from(src: sys::BredrData) -> Result<BredrData, Self::Error> {
        Ok(BredrData {
            address: src.address.ok_or(format_err!("No address"))?.into(),
            role_preference: src.role_preference,
            services: src.services.unwrap_or(vec![]).iter().map(|uuid| uuid.into()).collect(),
            link_key: src.link_key,
        })
    }
}

impl From<LeData> for control::LeData {
    fn from(src: LeData) -> control::LeData {
        let address_type = match &src.address {
            Address::Public(_) => control::AddressType::LePublic,
            Address::Random(_) => control::AddressType::LeRandom,
        };
        control::LeData {
            address: src.address.to_string(),
            address_type,
            connection_parameters: src
                .connection_parameters
                .map(|b| Box::new(compat::sys_conn_params_to_control(b))),
            services: src.services.into_iter().map(|uuid| uuid.to_string()).collect(),
            ltk: src.ltk.map(|k| Box::new(compat::ltk_to_control(k))),
            irk: src.irk.map(|k| Box::new(compat::peer_key_to_control(k))),
            csrk: src.csrk.map(|k| Box::new(compat::peer_key_to_control(k))),
        }
    }
}

impl From<LeData> for sys::LeData {
    fn from(src: LeData) -> sys::LeData {
        sys::LeData {
            address: Some(src.address.into()),
            connection_parameters: src.connection_parameters,
            services: Some(src.services.into_iter().map(|uuid| uuid.into()).collect()),
            ltk: src.ltk,
            irk: src.irk,
            csrk: src.csrk,
        }
    }
}

impl From<BredrData> for control::BredrData {
    fn from(src: BredrData) -> control::BredrData {
        let address = src.address.to_string();
        let piconet_leader =
            src.role_preference.map_or(false, |role| role == bt::ConnectionRole::Leader);
        let services = src.services.into_iter().map(|uuid| uuid.to_string()).collect();
        let link_key = src.link_key.map(|key| {
            Box::new(control::Ltk {
                key: compat::peer_key_to_control(key),
                key_size: key.security.encryption_key_size,
                // These values are LE-only, unused for Br/Edr. In sys::BredrData they are not stored,
                // but control::BredrData expects them
                ediv: 0,
                rand: 0,
            })
        });
        control::BredrData { address, piconet_leader, services, link_key }
    }
}

impl From<BredrData> for sys::BredrData {
    fn from(src: BredrData) -> sys::BredrData {
        sys::BredrData {
            address: Some(src.address.into()),
            role_preference: src.role_preference,
            services: Some(src.services.into_iter().map(|uuid| uuid.into()).collect()),
            link_key: src.link_key,
        }
    }
}

/// Data required to store a bond between a Peer and the system, so the bond can be restored later
#[derive(Clone, Debug, PartialEq)]
pub struct BondingData {
    pub identifier: PeerId,
    pub local_address: Address,
    pub name: Option<String>,
    // Valid Bonding Data must include at least one of LeData or BredrData
    pub data: OneOrBoth<LeData, BredrData>,
}

impl BondingData {
    pub fn le(&self) -> Option<&LeData> {
        self.data.left()
    }
    pub fn bredr(&self) -> Option<&BredrData> {
        self.data.right()
    }
}

impl TryFrom<control::BondingData> for BondingData {
    type Error = anyhow::Error;
    fn try_from(fidl: control::BondingData) -> Result<BondingData, Self::Error> {
        let le = fidl.le.map(|le| *le);
        let bredr = fidl.bredr.map(|bredr| *bredr);
        let data = match (le, bredr) {
            (Some(le), Some(bredr)) => OneOrBoth::Both(le.try_into()?, bredr.try_into()?),
            (Some(le), None) => OneOrBoth::Left(le.try_into()?),
            (None, Some(bredr)) => OneOrBoth::Right(bredr.try_into()?),
            (None, None) => {
                return Err(format_err!("Cannot store bond with neither LE nor Classic data"))
            }
        };
        Ok(BondingData {
            identifier: fidl.identifier.parse::<PeerId>()?,
            local_address: Address::public_from_str(&fidl.local_address)?,
            name: fidl.name,
            data,
        })
    }
}

impl From<BondingData> for control::BondingData {
    fn from(bd: BondingData) -> control::BondingData {
        let le = bd.le().map(|le| Box::new(le.clone().into()));
        let bredr = bd.bredr().map(|bredr| Box::new(bredr.clone().into()));
        control::BondingData {
            identifier: bd.identifier.to_string(),
            local_address: bd.local_address.to_string(),
            name: bd.name,
            le,
            bredr,
        }
    }
}

/// To convert an external BondingData to an internal Fuchsia bonding data, we must provide a
/// fuchsia PeerId to be used if the external source is missing one (for instance, it is being
/// migrated from a previous, non-Fuchsia system)
impl TryFrom<(sys::BondingData, PeerId)> for BondingData {
    type Error = anyhow::Error;
    fn try_from(from: (sys::BondingData, PeerId)) -> Result<BondingData, Self::Error> {
        let bond = from.0;
        let ident = from.1;
        let data = match (bond.le, bond.bredr) {
            (Some(le), Some(bredr)) => OneOrBoth::Both(le.try_into()?, bredr.try_into()?),
            (Some(le), None) => OneOrBoth::Left(le.try_into()?),
            (None, Some(bredr)) => OneOrBoth::Right(bredr.try_into()?),
            (None, None) => {
                return Err(format_err!("Cannot store bond with neither LE nor Classic data"))
            }
        };
        match bond.local_address {
            Some(local_address) => Ok(BondingData {
                identifier: bond.identifier.map(|id| id.into()).unwrap_or(ident),
                local_address: Address::from(local_address),
                name: bond.name,
                data,
            }),
            _ => Err(format_err!("No local address")),
        }
    }
}

impl From<BondingData> for sys::BondingData {
    fn from(bd: BondingData) -> sys::BondingData {
        let le = bd.le().map(|le| le.clone().into());
        let bredr = bd.bredr().map(|bredr| bredr.clone().into());
        sys::BondingData {
            identifier: Some(bd.identifier.into()),
            local_address: Some(bd.local_address.into()),
            name: bd.name,
            le,
            bredr,
        }
    }
}

pub struct Identity {
    pub host: sys::HostData,
    pub bonds: Vec<BondingData>,
}

impl From<Identity> for sys::Identity {
    fn from(src: Identity) -> sys::Identity {
        sys::Identity {
            host: Some(src.host),
            bonds: Some(src.bonds.into_iter().map(|i| i.into()).collect()),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::compat::*, super::*, crate::types::address::tests::any_public_address,
        fidl_fuchsia_bluetooth_control as control, fidl_fuchsia_bluetooth_sys as sys,
        proptest::option, proptest::prelude::*,
    };

    fn peer_key() -> sys::PeerKey {
        let data = sys::Key { value: [0; 16] };
        let security = sys::SecurityProperties {
            authenticated: false,
            secure_connections: true,
            encryption_key_size: 0,
        };
        sys::PeerKey { security, data }
    }

    fn control_ltk() -> control::Ltk {
        control::Ltk { key: compat::peer_key_to_control(peer_key()), key_size: 0, ediv: 0, rand: 0 }
    }

    fn new_bond() -> BondingData {
        let remote_key = peer_key();
        let ltk = sys::Ltk { key: remote_key.clone(), ediv: 1, rand: 2 };

        BondingData {
            identifier: PeerId(42),
            local_address: Address::Public([0, 0, 0, 0, 0, 0]),
            name: Some("name".into()),
            data: OneOrBoth::Both(
                LeData {
                    address: Address::Public([0, 0, 0, 0, 0, 0]),
                    connection_parameters: Some(sys::LeConnectionParameters {
                        connection_interval: 0,
                        connection_latency: 1,
                        supervision_timeout: 2,
                    }),
                    services: vec![],
                    ltk: Some(ltk.clone()),
                    irk: Some(remote_key.clone()),
                    csrk: Some(remote_key.clone()),
                },
                BredrData {
                    address: Address::Public([0, 0, 0, 0, 0, 0]),
                    role_preference: Some(bt::ConnectionRole::Leader),
                    services: vec![],
                    link_key: Some(remote_key.clone()),
                },
            ),
        }
    }

    #[test]
    fn bonding_data_conversion() {
        let bond = new_bond();
        assert_eq!(
            Ok(bond.clone()),
            BondingData::try_from(control::BondingData::from(bond.clone()))
                .map_err(|e| e.to_string())
        )
    }

    fn any_security_properties() -> impl Strategy<Value = sys::SecurityProperties> {
        any::<(bool, bool, u8)>().prop_map(
            |(authenticated, secure_connections, encryption_key_size)| sys::SecurityProperties {
                authenticated,
                secure_connections,
                encryption_key_size,
            },
        )
    }

    fn any_key() -> impl Strategy<Value = sys::Key> {
        any::<[u8; 16]>().prop_map(|value| sys::Key { value })
    }

    fn any_peer_key() -> impl Strategy<Value = sys::PeerKey> {
        (any_security_properties(), any_key())
            .prop_map(|(security, data)| sys::PeerKey { security, data })
    }

    fn any_ltk() -> impl Strategy<Value = sys::Ltk> {
        (any_peer_key(), any::<u16>(), any::<u64>()).prop_map(|(key, ediv, rand)| sys::Ltk {
            key,
            ediv,
            rand,
        })
    }

    fn any_connection_params() -> impl Strategy<Value = sys::LeConnectionParameters> {
        (any::<u16>(), any::<u16>(), any::<u16>()).prop_map(
            |(connection_interval, connection_latency, supervision_timeout)| {
                sys::LeConnectionParameters {
                    connection_interval,
                    connection_latency,
                    supervision_timeout,
                }
            },
        )
    }

    fn any_connection_role() -> impl Strategy<Value = bt::ConnectionRole> {
        prop_oneof![Just(bt::ConnectionRole::Leader), Just(bt::ConnectionRole::Follower)]
    }

    // TODO(36378) Note: We don't generate data with a None role_preference, as these can't be
    // safely roundtripped to control::BredrData. This can be removed when the control api is
    // retired.
    fn any_bredr_data() -> impl Strategy<Value = BredrData> {
        (any_public_address(), any_connection_role(), option::of(any_peer_key())).prop_map(
            |(address, role_preference, link_key)| {
                let role_preference = Some(role_preference);
                BredrData { address, role_preference, services: vec![], link_key }
            },
        )
    }

    fn any_le_data() -> impl Strategy<Value = LeData> {
        (
            any_public_address(),
            option::of(any_connection_params()),
            option::of(any_ltk()),
            option::of(any_peer_key()),
            option::of(any_peer_key()),
        )
            .prop_map(|(address, connection_parameters, ltk, irk, csrk)| LeData {
                address,
                connection_parameters,
                services: vec![],
                ltk,
                irk,
                csrk,
            })
    }

    fn any_bonding_data() -> impl Strategy<Value = BondingData> {
        let any_data = prop_oneof![
            any_le_data().prop_map(OneOrBoth::Left),
            any_bredr_data().prop_map(OneOrBoth::Right),
            (any_le_data(), any_bredr_data()).prop_map(|(le, bredr)| OneOrBoth::Both(le, bredr)),
        ];
        (any::<u64>(), any_public_address(), option::of("[a-zA-Z][a-zA-Z0-9_]*"), any_data)
            .prop_map(|(ident, local_address, name, data)| {
                let identifier = PeerId(ident);
                BondingData { identifier, local_address, name, data }
            })
    }

    proptest! {
        #[test]
        fn bredr_data_sys_roundtrip(data in any_bredr_data()) {
            let sys_bredr_data: sys::BredrData = data.clone().into();
            assert_eq!(Ok(data), sys_bredr_data.try_into().map_err(|e: anyhow::Error| e.to_string()));
        }
        #[test]
        fn bredr_data_control_roundtrip(data in any_bredr_data()) {
            let control_bredr_data: control::BredrData = data.clone().into();
            assert_eq!(Ok(data), control_bredr_data.try_into().map_err(|e: anyhow::Error| e.to_string()));
        }

        #[test]
        fn le_data_sys_roundtrip(data in any_le_data()) {
            let sys_le_data: sys::LeData = data.clone().into();
            assert_eq!(Ok(data), sys_le_data.try_into().map_err(|e: anyhow::Error| e.to_string()));
        }
        #[test]
        fn le_data_control_roundtrip(data in any_le_data()) {
            let control_le_data: control::LeData = data.clone().into();
            assert_eq!(Ok(data), control_le_data.try_into().map_err(|e: anyhow::Error| e.to_string()));
        }
        #[test]
        fn bonding_data_sys_roundtrip(data in any_bonding_data()) {
            let peer_id = data.identifier;
            let sys_bonding_data: sys::BondingData = data.clone().into();
            assert_eq!(Ok(data), (sys_bonding_data, peer_id).try_into().map_err(|e: anyhow::Error| e.to_string()));
        }
        #[test]
        fn bonding_data_control_roundtrip(data in any_bonding_data()) {
            let control_bonding_data: control::BondingData = data.clone().into();
            assert_eq!(Ok(data), control_bonding_data.try_into().map_err(|e: anyhow::Error| e.to_string()));
        }
    }
}
