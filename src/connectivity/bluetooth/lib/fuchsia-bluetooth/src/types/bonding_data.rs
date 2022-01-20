// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl_fuchsia_bluetooth as bt, fidl_fuchsia_bluetooth_sys as sys, fuchsia_inspect as inspect,
    std::convert::{TryFrom, TryInto},
};

use crate::{
    inspect::{InspectData, IsInspectable, ToProperty},
    types::{uuid::Uuid, Address, OneOrBoth, PeerId},
};

#[derive(Debug)]
struct LeInspect {
    _inspect: inspect::Node,
    _services: inspect::StringProperty,
    _connection_interval: Option<inspect::UintProperty>,
    _connection_latency: Option<inspect::UintProperty>,
    _supervision_timeout: Option<inspect::UintProperty>,
    _peer_ltk_authenticated: inspect::UintProperty,
    _peer_ltk_secure_connections: inspect::UintProperty,
    _peer_ltk_encryption_key_size: Option<inspect::UintProperty>,
    _local_ltk_authenticated: inspect::UintProperty,
    _local_ltk_secure_connections: inspect::UintProperty,
    _local_ltk_encryption_key_size: Option<inspect::UintProperty>,
    _irk_authenticated: inspect::UintProperty,
    _irk_secure_connections: inspect::UintProperty,
    _irk_encryption_key_size: Option<inspect::UintProperty>,
    _csrk_authenticated: inspect::UintProperty,
    _csrk_secure_connections: inspect::UintProperty,
    _csrk_encryption_key_size: Option<inspect::UintProperty>,
}

impl LeInspect {
    fn new(d: &LeBondData, inspect: inspect::Node) -> LeInspect {
        LeInspect {
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

            _peer_ltk_authenticated: inspect.create_uint(
                "peer_ltk_authenticated",
                d.peer_ltk.as_ref().map(|ltk| ltk.key.security.authenticated).to_property(),
            ),
            _peer_ltk_secure_connections: inspect.create_uint(
                "peer_ltk_secure_connections",
                d.peer_ltk.as_ref().map(|ltk| ltk.key.security.secure_connections).to_property(),
            ),
            _peer_ltk_encryption_key_size: d.peer_ltk.as_ref().map(|ltk| {
                inspect.create_uint(
                    "peer_ltk_encryption_key_size",
                    ltk.key.security.encryption_key_size as u64,
                )
            }),

            _local_ltk_authenticated: inspect.create_uint(
                "local_ltk_authenticated",
                d.local_ltk.as_ref().map(|ltk| ltk.key.security.authenticated).to_property(),
            ),
            _local_ltk_secure_connections: inspect.create_uint(
                "local_ltk_secure_connections",
                d.local_ltk.as_ref().map(|ltk| ltk.key.security.secure_connections).to_property(),
            ),
            _local_ltk_encryption_key_size: d.local_ltk.as_ref().map(|ltk| {
                inspect.create_uint(
                    "local_ltk_encryption_key_size",
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
    fn new(d: &BredrBondData, inspect: inspect::Node) -> BredrInspect {
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
    _address_type: inspect::StringProperty,
    _le_inspect: Option<LeInspect>,
    _bredr_inspect: Option<BredrInspect>,
}

impl InspectData<BondingData> for BondingDataInspect {
    fn new(bd: &BondingData, inspect: inspect::Node) -> BondingDataInspect {
        BondingDataInspect {
            _address_type: inspect.create_string("address_type", bd.address.address_type_string()),
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
pub struct LeBondData {
    /// The peer's preferred connection parameters, if known.
    pub connection_parameters: Option<sys::LeConnectionParameters>,
    /// Known GATT service UUIDs.
    pub services: Vec<Uuid>,
    /// LE long-term key generated and distributed by the peer device. This key is used when the
    /// peer is the follower (i.e. the peer is in the LE peripheral role).
    ///
    /// Note: In LE legacy pairing, both sides are allowed to generate and distribute a link key.
    /// In Secure Connections pairing, both sides generate the same LTK and hence the `peer_ltk` and
    /// `local_ltk` values are identical.
    pub peer_ltk: Option<sys::Ltk>,
    /// LE long-term key generated and distributed by the local bt-host. This key is used when the
    /// peer is the leader (i.e. the peer in the LE central role).
    ///
    /// Note: In LE legacy pairing, both sides are allowed to generate and distribute a link key.
    /// In Secure Connections pairing, both sides generate the same LTK and hence the `peer_ltk` and
    /// `local_ltk` values are identical.
    pub local_ltk: Option<sys::Ltk>,
    /// Identity Resolving RemoteKey used to generate and resolve random addresses.
    pub irk: Option<sys::PeerKey>,
    /// Connection Signature Resolving RemoteKey used for data signing without encryption.
    pub csrk: Option<sys::PeerKey>,
}

/// Bluetooth BR/EDR (Classic) specific bonding data
#[derive(Clone, Debug, PartialEq)]
pub struct BredrBondData {
    /// True if the peer prefers to lead the piconet. This is determined by role switch procedures.
    /// Paging and connecting from a peer does not automatically set this flag.
    pub role_preference: Option<bt::ConnectionRole>,
    /// Known service UUIDs obtained from EIR data or SDP.
    pub services: Vec<Uuid>,
    /// The semi-permanent BR/EDR key. Present if link was paired with Secure Simple Pairing or
    /// stronger.
    pub link_key: Option<sys::PeerKey>,
}

impl From<sys::LeBondData> for LeBondData {
    fn from(src: sys::LeBondData) -> Self {
        Self {
            connection_parameters: src.connection_parameters,
            services: src.services.unwrap_or(vec![]).iter().map(Into::into).collect(),
            peer_ltk: src.peer_ltk,
            local_ltk: src.local_ltk,
            irk: src.irk,
            csrk: src.csrk,
        }
    }
}

impl From<sys::BredrBondData> for BredrBondData {
    fn from(src: sys::BredrBondData) -> Self {
        Self {
            role_preference: src.role_preference,
            services: src.services.unwrap_or(vec![]).iter().map(Into::into).collect(),
            link_key: src.link_key,
        }
    }
}

impl From<LeBondData> for sys::LeBondData {
    fn from(src: LeBondData) -> Self {
        sys::LeBondData {
            connection_parameters: src.connection_parameters,
            services: Some(src.services.into_iter().map(|uuid| uuid.into()).collect()),
            peer_ltk: src.peer_ltk,
            local_ltk: src.local_ltk,
            irk: src.irk,
            csrk: src.csrk,
            ..sys::LeBondData::EMPTY
        }
    }
}

impl From<BredrBondData> for sys::BredrBondData {
    fn from(src: BredrBondData) -> Self {
        sys::BredrBondData {
            role_preference: src.role_preference,
            services: Some(src.services.into_iter().map(|uuid| uuid.into()).collect()),
            link_key: src.link_key,
            ..sys::BredrBondData::EMPTY
        }
    }
}

/// Data required to store a bond between a Peer and the system, so the bond can be restored later
#[derive(Clone, Debug, PartialEq)]
pub struct BondingData {
    /// The persisted unique identifier for this peer.
    pub identifier: PeerId,

    /// The identity address of the peer.
    pub address: Address,

    /// The local bt-host identity address that this bond is associated with.
    pub local_address: Address,

    /// The device name obtained using general discovery and name discovery procedures.
    pub name: Option<String>,

    /// Valid Bonding Data must include at least one of LeBondData or BredrBondData.
    pub data: OneOrBoth<LeBondData, BredrBondData>,
}

impl BondingData {
    pub fn le(&self) -> Option<&LeBondData> {
        self.data.left()
    }
    pub fn bredr(&self) -> Option<&BredrBondData> {
        self.data.right()
    }
}

impl TryFrom<sys::BondingData> for BondingData {
    type Error = anyhow::Error;
    fn try_from(fidl: sys::BondingData) -> Result<BondingData, Self::Error> {
        let fidl_clone = fidl.clone();
        let data = match (fidl_clone.le_bond, fidl_clone.bredr_bond) {
            (Some(le_bond), Some(bredr_bond)) => OneOrBoth::Both(le_bond.into(), bredr_bond.into()),
            (Some(le_bond), None) => OneOrBoth::Left(le_bond.into()),
            (None, Some(bredr_bond)) => OneOrBoth::Right(bredr_bond.into()),
            _ => {
                return Err(format_err!("bond missing transport-specific data: {:?}", fidl));
            }
        };

        Ok(BondingData {
            identifier: fidl.identifier.ok_or(format_err!("identifier missing"))?.into(),
            address: fidl.address.ok_or(format_err!("address missing"))?.into(),
            local_address: fidl.local_address.ok_or(format_err!("local address missing"))?.into(),
            name: fidl.name,
            data,
        })
    }
}

/// To convert an external BondingData to an internal Fuchsia bonding data, we must provide a
/// fuchsia PeerId to be used if the external source is missing one (for instance, it is being
/// migrated from a previous, non-Fuchsia system)
impl TryFrom<(sys::BondingData, PeerId)> for BondingData {
    type Error = anyhow::Error;
    fn try_from(from: (sys::BondingData, PeerId)) -> Result<BondingData, Self::Error> {
        let mut bond = from.0;
        let id = match bond.identifier {
            Some(id) => id.into(),
            None => from.1,
        };
        bond.identifier = Some(id.into());
        bond.try_into()
    }
}

impl From<BondingData> for sys::BondingData {
    fn from(bd: BondingData) -> sys::BondingData {
        let le_bond = bd.le().map(|le| le.clone().into());
        let bredr_bond = bd.bredr().map(|bredr| bredr.clone().into());
        sys::BondingData {
            identifier: Some(bd.identifier.into()),
            address: Some(bd.address.into()),
            local_address: Some(bd.local_address.into()),
            name: bd.name,
            le_bond,
            bredr_bond,
            ..sys::BondingData::EMPTY
        }
    }
}

/// Persisted data for a local bt-host.
#[derive(Clone, Debug, PartialEq)]
pub struct HostData {
    /// A local IRK that is distributed to peers and used to generate RPAs when in LE peripheral
    /// mode.
    pub irk: Option<sys::Key>,
}

impl From<HostData> for sys::HostData {
    fn from(src: HostData) -> sys::HostData {
        sys::HostData { irk: src.irk, ..sys::HostData::EMPTY }
    }
}

impl From<sys::HostData> for HostData {
    fn from(src: sys::HostData) -> HostData {
        HostData { irk: src.irk }
    }
}

pub struct Identity {
    pub host: HostData,
    pub bonds: Vec<BondingData>,
}

impl From<Identity> for sys::Identity {
    fn from(src: Identity) -> sys::Identity {
        sys::Identity {
            host: Some(src.host.into()),
            bonds: Some(src.bonds.into_iter().map(|i| i.into()).collect()),
            ..sys::Identity::EMPTY
        }
    }
}

/// This module defines a BondingData test strategy generator for use with proptest.
pub mod proptest_util {
    use super::*;
    use crate::types::address::proptest_util::any_address;
    use proptest::{option, prelude::*};

    pub fn any_bonding_data() -> impl Strategy<Value = BondingData> {
        let any_data = prop_oneof![
            any_le_data().prop_map(OneOrBoth::Left),
            any_bredr_data().prop_map(OneOrBoth::Right),
            (any_le_data(), any_bredr_data()).prop_map(|(le, bredr)| OneOrBoth::Both(le, bredr)),
        ];
        (any::<u64>(), any_address(), any_address(), option::of("[a-zA-Z][a-zA-Z0-9_]*"), any_data)
            .prop_map(|(ident, address, local_address, name, data)| {
                let identifier = PeerId(ident);
                BondingData { identifier, address, local_address, name, data }
            })
    }

    pub(crate) fn any_bredr_data() -> impl Strategy<Value = BredrBondData> {
        (option::of(any_connection_role()), option::of(any_peer_key())).prop_map(
            |(role_preference, link_key)| BredrBondData {
                role_preference,
                services: vec![],
                link_key,
            },
        )
    }

    pub(crate) fn any_le_data() -> impl Strategy<Value = LeBondData> {
        (
            option::of(any_connection_params()),
            option::of(any_ltk()),
            option::of(any_ltk()),
            option::of(any_peer_key()),
            option::of(any_peer_key()),
        )
            .prop_map(|(connection_parameters, peer_ltk, local_ltk, irk, csrk)| {
                LeBondData {
                    connection_parameters,
                    services: vec![],
                    peer_ltk,
                    local_ltk,
                    irk,
                    csrk,
                }
            })
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
}

pub mod example {
    use super::*;

    pub fn peer_key() -> sys::PeerKey {
        let data = sys::Key { value: [0; 16] };
        let security = sys::SecurityProperties {
            authenticated: false,
            secure_connections: true,
            encryption_key_size: 0,
        };
        sys::PeerKey { security, data }
    }

    pub fn bond(host_addr: Address, peer_addr: Address) -> BondingData {
        let remote_key = example::peer_key();
        let ltk = sys::Ltk { key: remote_key.clone(), ediv: 1, rand: 2 };

        BondingData {
            identifier: PeerId(42),
            address: peer_addr,
            local_address: host_addr,
            name: Some("name".into()),
            data: OneOrBoth::Both(
                LeBondData {
                    connection_parameters: Some(sys::LeConnectionParameters {
                        connection_interval: 0,
                        connection_latency: 1,
                        supervision_timeout: 2,
                    }),
                    services: vec![],
                    peer_ltk: Some(ltk.clone()),
                    local_ltk: Some(ltk.clone()),
                    irk: Some(remote_key.clone()),
                    csrk: Some(remote_key.clone()),
                },
                BredrBondData {
                    role_preference: Some(bt::ConnectionRole::Leader),
                    services: vec![],
                    link_key: Some(remote_key.clone()),
                },
            ),
        }
    }
}

#[cfg(test)]
pub mod tests {
    use {super::*, fidl_fuchsia_bluetooth_sys as sys};

    // Tests for conversions from fuchsia.bluetooth.sys API
    mod from_sys {
        use super::*;
        use assert_matches::assert_matches;

        fn default_ltk() -> sys::Ltk {
            sys::Ltk {
                key: sys::PeerKey {
                    security: sys::SecurityProperties {
                        authenticated: false,
                        secure_connections: false,
                        encryption_key_size: 16,
                    },
                    data: sys::Key {
                        value: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                    },
                },
                ediv: 0,
                rand: 0,
            }
        }

        fn test_sys_bond(
            le_bond: &Option<sys::LeBondData>,
            bredr_bond: &Option<sys::BredrBondData>,
        ) -> sys::BondingData {
            sys::BondingData {
                identifier: Some(bt::PeerId { value: 42 }),
                address: Some(bt::Address {
                    type_: bt::AddressType::Public,
                    bytes: [0, 0, 0, 0, 0, 0],
                }),
                local_address: Some(bt::Address {
                    type_: bt::AddressType::Public,
                    bytes: [0, 0, 0, 0, 0, 0],
                }),
                name: Some("name".into()),
                le_bond: le_bond.clone(),
                bredr_bond: bredr_bond.clone(),
                ..sys::BondingData::EMPTY
            }
        }

        #[test]
        fn id_missing() {
            let src = sys::BondingData {
                identifier: None,
                address: Some(bt::Address {
                    type_: bt::AddressType::Random,
                    bytes: [1, 2, 3, 4, 5, 6],
                }),
                le_bond: Some(sys::LeBondData::EMPTY),
                ..sys::BondingData::EMPTY
            };
            let result = BondingData::try_from(src);
            assert_eq!(
                Err("identifier missing".to_string()),
                result.map_err(|e| format!("{:?}", e))
            );
        }

        #[test]
        fn address_missing() {
            let src = sys::BondingData {
                identifier: Some(bt::PeerId { value: 1 }),
                le_bond: Some(sys::LeBondData::EMPTY),
                bredr_bond: Some(sys::BredrBondData::EMPTY),
                ..sys::BondingData::EMPTY
            };
            let result = BondingData::try_from(src);
            assert_matches!(result.map_err(|e| format!("{:?}", e)), Err(e) if e.contains("address missing"));
        }

        #[test]
        fn use_address() {
            let addr = bt::Address { type_: bt::AddressType::Public, bytes: [1, 2, 3, 4, 5, 6] };
            let src = sys::BondingData {
                identifier: Some(bt::PeerId { value: 1 }),
                address: Some(addr.clone()),
                local_address: Some(bt::Address {
                    type_: bt::AddressType::Public,
                    bytes: [1, 0, 0, 0, 0, 0],
                }),
                le_bond: Some(sys::LeBondData::EMPTY),
                bredr_bond: Some(sys::BredrBondData::EMPTY),
                ..sys::BondingData::EMPTY
            };
            let result =
                BondingData::try_from(src).expect("failed to convert from sys.BondingData");
            assert_eq!(result.address, addr.into());
        }

        #[test]
        fn use_peer_and_local_ltk() {
            let ltk1 = default_ltk();
            let mut ltk2 = default_ltk();
            ltk2.key.security.authenticated = true;

            let src = sys::BondingData {
                identifier: Some(bt::PeerId { value: 1 }),
                address: Some(bt::Address {
                    type_: bt::AddressType::Public,
                    bytes: [1, 2, 3, 4, 5, 6],
                }),
                local_address: Some(bt::Address {
                    type_: bt::AddressType::Public,
                    bytes: [1, 0, 0, 0, 0, 0],
                }),
                le_bond: Some(sys::LeBondData {
                    local_ltk: Some(ltk1.clone()),
                    peer_ltk: Some(ltk2.clone()),
                    ..sys::LeBondData::EMPTY
                }),
                bredr_bond: Some(sys::BredrBondData::EMPTY),
                ..sys::BondingData::EMPTY
            };

            let result =
                BondingData::try_from(src).expect("failed to convert from sys.BondingData");
            let result_le = result.le().expect("expected LE data");
            assert_eq!(result_le.local_ltk, Some(ltk1));
            assert_eq!(result_le.peer_ltk, Some(ltk2));
        }

        #[test]
        fn rejects_missing_transport_specific() {
            let le_bond = Some(sys::LeBondData::EMPTY);
            let bredr_bond = Some(sys::BredrBondData::EMPTY);

            // Valid combinations of bonding data
            assert!(BondingData::try_from(test_sys_bond(&le_bond, &None)).is_ok());
            assert!(BondingData::try_from(test_sys_bond(&None, &bredr_bond)).is_ok());
            assert!(BondingData::try_from(test_sys_bond(&le_bond, &bredr_bond)).is_ok());

            assert!(BondingData::try_from(test_sys_bond(&None, &None)).is_err());
        }
    }

    // The test cases below use proptest to exercise round-trip conversions between FIDL and the
    // library type across several permutations.
    mod roundtrip {
        use super::{
            proptest_util::{any_bonding_data, any_bredr_data, any_le_data},
            *,
        };
        use proptest::prelude::*;

        proptest! {
            #[test]
            fn bredr_data_sys_roundtrip(data in any_bredr_data()) {
                let sys_bredr_data: sys::BredrBondData = data.clone().into();
                assert_eq!(data, sys_bredr_data.into());
            }
            #[test]
            fn le_data_sys_roundtrip(data in any_le_data()) {
                let sys_le_data: sys::LeBondData = data.clone().into();
                assert_eq!(data, sys_le_data.into());
            }
            #[test]
            fn bonding_data_sys_roundtrip(data in any_bonding_data()) {
                let peer_id = data.identifier;
                let sys_bonding_data: sys::BondingData = data.clone().into();
                assert_eq!(Ok(data), (sys_bonding_data, peer_id).try_into().map_err(|e: anyhow::Error| e.to_string()));
            }
        }
    }
}
