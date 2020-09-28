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
    fn new(d: &LeData, inspect: inspect::Node) -> LeInspect {
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
pub struct LeData {
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

impl LeData {
    fn into_control(src: LeData, address: &Address) -> control::LeData {
        let address_type = match address {
            Address::Public(_) => control::AddressType::LePublic,
            Address::Random(_) => control::AddressType::LeRandom,
        };
        let ltk = {
            // TODO(fxbug.dev/2411): bt-host currently supports the exchange of only a single LTK during
            // LE legacy pairing and still reports this in the singular `ltk` field of the control
            // library `BondingData` type. Until bt-host's dependency on control gets removed, we
            // map both peer and local LTKs in the new internal representations to this singular
            // LTK. Remove this workaround and the control conversions once they are no longer
            // needed for bt-host compatibility.
            assert!(src.peer_ltk == src.local_ltk);
            src.peer_ltk.map(|k| Box::new(compat::ltk_to_control(k)))
        };
        control::LeData {
            address: address.to_string(),
            address_type,
            connection_parameters: src
                .connection_parameters
                .map(|b| Box::new(compat::sys_conn_params_to_control(b))),
            services: src.services.into_iter().map(|uuid| uuid.to_string()).collect(),
            ltk,
            irk: src.irk.map(|k| Box::new(compat::peer_key_to_control(k))),
            csrk: src.csrk.map(|k| Box::new(compat::peer_key_to_control(k))),
        }
    }
}

/// Bluetooth BR/EDR (Classic) specific bonding data
#[derive(Clone, Debug, PartialEq)]
pub struct BredrData {
    /// True if the peer prefers to lead the piconet. This is determined by role switch procedures.
    /// Paging and connecting from a peer does not automatically set this flag.
    pub role_preference: Option<bt::ConnectionRole>,
    /// Known service UUIDs obtained from EIR data or SDP.
    pub services: Vec<Uuid>,
    /// The semi-permanent BR/EDR key. Present if link was paired with Secure Simple Pairing or
    /// stronger.
    pub link_key: Option<sys::PeerKey>,
}

impl BredrData {
    fn into_control(src: BredrData, address: &Address) -> control::BredrData {
        let address = address.to_string();
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

/// TODO(fxbug.dev/36378) - Compatibility functions to convert from the to-be-deprecated Control api types
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

    pub fn local_key_from_control(src: control::LocalKey) -> sys::Key {
        sys::Key { value: src.value }
    }

    pub fn local_key_to_control(src: sys::Key) -> control::LocalKey {
        control::LocalKey { value: src.value }
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

fn address_from_ctrl_le_data(src: &control::LeData) -> Result<Address, anyhow::Error> {
    match src.address_type {
        control::AddressType::LePublic => Ok(Address::public_from_str(&src.address)?),
        control::AddressType::LeRandom => Ok(Address::random_from_str(&src.address)?),
        _ => Err(format_err!("Invalid address type, expected LE_PUBLIC or LE_RANDOM")),
    }
}

impl TryFrom<control::LeData> for LeData {
    type Error = anyhow::Error;
    fn try_from(src: control::LeData) -> Result<LeData, Self::Error> {
        let services: Result<Vec<Uuid>, uuid::parser::ParseError> =
            src.services.iter().map(|s| s.parse::<Uuid>()).collect();
        Ok(LeData {
            connection_parameters: src
                .connection_parameters
                .map(|params| compat::sys_conn_params_from_control(*params)),
            services: services?,
            // TODO(fxbug.dev/35008): For now we map the singular control LTK to both the local and peer
            // types, which should match the current behavior of bt-host. Remove this logic once
            // bt-host generates separate local and peer keys during legacy pairing.
            peer_ltk: src.ltk.clone().map(|ltk| compat::ltk_from_control(*ltk)),
            local_ltk: src.ltk.map(|ltk| compat::ltk_from_control(*ltk)),
            irk: src.irk.map(|irk| compat::peer_key_from_control(*irk)),
            csrk: src.csrk.map(|csrk| compat::peer_key_from_control(*csrk)),
        })
    }
}
impl TryFrom<sys::LeData> for LeData {
    type Error = anyhow::Error;
    fn try_from(src: sys::LeData) -> Result<LeData, Self::Error> {
        // If one of the new `peer_ltk` and `local_ltk` fields are present, then we use those.
        // Otherwise we default to the deprecated `ltk` field for backwards compatibility.
        let (peer_ltk, local_ltk) = match (src.peer_ltk, src.local_ltk) {
            (None, None) => (src.ltk, src.ltk),
            (p, l) => (p, l),
        };
        Ok(LeData {
            connection_parameters: src.connection_parameters,
            services: src.services.unwrap_or(vec![]).iter().map(|uuid| uuid.into()).collect(),
            peer_ltk,
            local_ltk,
            irk: src.irk,
            csrk: src.csrk,
        })
    }
}

impl TryFrom<control::BredrData> for BredrData {
    type Error = anyhow::Error;
    fn try_from(src: control::BredrData) -> Result<BredrData, Self::Error> {
        let role_preference = Some(if src.piconet_leader {
            bt::ConnectionRole::Leader
        } else {
            bt::ConnectionRole::Follower
        });
        let services = src.services.iter().map(|uuid_str| uuid_str.parse()).collect_results()?;
        let link_key = src.link_key.map(|ltk| compat::peer_key_from_control(ltk.key));
        Ok(BredrData { role_preference, services, link_key })
    }
}
impl TryFrom<sys::BredrData> for BredrData {
    type Error = anyhow::Error;
    fn try_from(src: sys::BredrData) -> Result<BredrData, Self::Error> {
        Ok(BredrData {
            role_preference: src.role_preference,
            services: src.services.unwrap_or(vec![]).iter().map(|uuid| uuid.into()).collect(),
            link_key: src.link_key,
        })
    }
}

impl From<LeData> for sys::LeData {
    fn from(src: LeData) -> sys::LeData {
        sys::LeData {
            address: None,
            connection_parameters: src.connection_parameters,
            services: Some(src.services.into_iter().map(|uuid| uuid.into()).collect()),
            // The LTK field is deprecated and is not necessary for internal conversions to sys
            // types.
            //
            // Note: when converting in the opposite direction (from sys::LeData to
            // LeData) the `ltk` field will be mapped to both `peer_ltk` and `local_ltk`. This
            // means that converting from a sys::LeData to LeData and back is no longer idempotent.
            ltk: None,
            peer_ltk: src.peer_ltk,
            local_ltk: src.local_ltk,
            irk: src.irk,
            csrk: src.csrk,
        }
    }
}

impl From<BredrData> for sys::BredrData {
    fn from(src: BredrData) -> sys::BredrData {
        sys::BredrData {
            address: None,
            role_preference: src.role_preference,
            services: Some(src.services.into_iter().map(|uuid| uuid.into()).collect()),
            link_key: src.link_key,
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

    /// Valid Bonding Data must include at least one of LeData or BredrData
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
        let (data, address) = match (le, bredr) {
            (Some(le), Some(bredr)) => {
                // If bonding data for both transports is present, then their transport-specific
                // addresses must be identical and present for this to be a valid dual-mode peer.
                let le_addr = address_from_ctrl_le_data(&le)?;
                let bredr_addr = Address::public_from_str(&bredr.address)?;
                if le_addr != bredr_addr {
                    return Err(format_err!(
                        "LE and BR/EDR addresses for dual-mode bond do not match!"
                    ));
                }
                (OneOrBoth::Both(le.try_into()?, bredr.try_into()?), le_addr)
            }
            (Some(le), None) => {
                let addr = address_from_ctrl_le_data(&le)?;
                (OneOrBoth::Left(le.try_into()?), addr)
            }
            (None, Some(bredr)) => {
                let addr = Address::public_from_str(&bredr.address)?;
                (OneOrBoth::Right(bredr.try_into()?), addr)
            }
            (None, None) => {
                return Err(format_err!("Cannot store bond with neither LE nor Classic data"))
            }
        };
        Ok(BondingData {
            identifier: fidl.identifier.parse::<PeerId>()?,
            address,
            local_address: Address::public_from_str(&fidl.local_address)?,
            name: fidl.name,
            data,
        })
    }
}

impl From<BondingData> for control::BondingData {
    fn from(bd: BondingData) -> control::BondingData {
        let le = bd.le().map(|le| Box::new(LeData::into_control(le.clone(), &bd.address)));
        let bredr =
            bd.bredr().map(|bredr| Box::new(BredrData::into_control(bredr.clone(), &bd.address)));
        control::BondingData {
            identifier: bd.identifier.to_string(),
            local_address: bd.local_address.to_string(),
            name: bd.name,
            le,
            bredr,
        }
    }
}

impl TryFrom<sys::BondingData> for BondingData {
    type Error = anyhow::Error;
    fn try_from(fidl: sys::BondingData) -> Result<BondingData, Self::Error> {
        let (data, transport_address) = match (fidl.le, fidl.bredr) {
            (Some(le), Some(bredr)) => {
                // If bonding data for both transports is present, then their transport-specific
                // addresses must be identical and present for this to be a valid dual-mode peer.
                if le.address != bredr.address {
                    return Err(format_err!("LE and BR/EDR addresses do not match"));
                }
                let addr = le.address.clone();
                (OneOrBoth::Both(le.try_into()?, bredr.try_into()?), addr)
            }
            (Some(le), None) => {
                let addr = le.address.clone();
                (OneOrBoth::Left(le.try_into()?), addr)
            }
            (None, Some(bredr)) => {
                let addr = bredr.address.clone();
                (OneOrBoth::Right(bredr.try_into()?), addr)
            }
            (None, None) => {
                return Err(format_err!("transport-specific data missing"));
            }
        };

        // If |fidl| contains a non-transport specific address then we always use that. Otherwise,
        // we fallback to the per-transport address for backwards compatibility with older clients.
        let address = match fidl.address {
            Some(address) => address,
            None => transport_address.ok_or(format_err!("address missing"))?,
        };

        Ok(BondingData {
            identifier: fidl.identifier.ok_or(format_err!("identifier missing"))?.into(),
            address: address.into(),
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
        let le = bd.le().map(|le| le.clone().into());
        let bredr = bd.bredr().map(|bredr| bredr.clone().into());
        sys::BondingData {
            identifier: Some(bd.identifier.into()),
            address: Some(bd.address.into()),
            local_address: Some(bd.local_address.into()),
            name: bd.name,
            le,
            bredr,
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
        sys::HostData { irk: src.irk }
    }
}

impl From<sys::HostData> for HostData {
    fn from(src: sys::HostData) -> HostData {
        HostData { irk: src.irk }
    }
}

impl From<control::HostData> for HostData {
    fn from(src: control::HostData) -> HostData {
        HostData { irk: src.irk.map(|k| compat::local_key_from_control(*k)) }
    }
}

impl From<HostData> for control::HostData {
    fn from(src: HostData) -> control::HostData {
        control::HostData { irk: src.irk.map(|k| Box::new(compat::local_key_to_control(k))) }
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
        }
    }
}

/// This module defines a BondingData test strategy generator for use with proptest.
pub mod proptest_util {
    use super::*;
    use crate::types::address::proptest_util::{any_address, any_public_address};
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

    // TODO(fxbug.dev/36378) Note: We don't generate data with a None role_preference, as these can't be
    // safely roundtripped to control::BredrData. This can be removed when the control api is
    // retired.
    pub(crate) fn any_bredr_data() -> impl Strategy<Value = BredrData> {
        (any_connection_role(), option::of(any_peer_key())).prop_map(
            |(role_preference, link_key)| {
                let role_preference = Some(role_preference);
                BredrData { role_preference, services: vec![], link_key }
            },
        )
    }

    pub(crate) fn any_le_data() -> impl Strategy<Value = LeData> {
        (
            option::of(any_connection_params()),
            option::of(any_ltk()),
            option::of(any_ltk()),
            option::of(any_peer_key()),
            option::of(any_peer_key()),
        )
            .prop_map(|(connection_parameters, peer_ltk, local_ltk, irk, csrk)| LeData {
                connection_parameters,
                services: vec![],
                peer_ltk,
                local_ltk,
                irk,
                csrk,
            })
    }

    // TODO(fxbug.dev/35008): The control library conversions expect `local_ltk` and `peer_ltk` to be the
    // same. We emulate that invariant here.
    pub(crate) fn any_le_data_for_control_test() -> impl Strategy<Value = LeData> {
        (
            option::of(any_connection_params()),
            option::of(any_ltk()),
            option::of(any_peer_key()),
            option::of(any_peer_key()),
        )
            .prop_map(|(connection_parameters, ltk, irk, csrk)| LeData {
                connection_parameters,
                services: vec![],
                peer_ltk: ltk,
                local_ltk: ltk,
                irk,
                csrk,
            })
    }

    pub(crate) fn any_bonding_data_for_control_test() -> impl Strategy<Value = BondingData> {
        let any_data = prop_oneof![
            any_le_data_for_control_test().prop_map(OneOrBoth::Left),
            any_bredr_data().prop_map(OneOrBoth::Right),
            (any_le_data_for_control_test(), any_bredr_data())
                .prop_map(|(le, bredr)| OneOrBoth::Both(le, bredr)),
        ];
        (
            any::<u64>(),
            any_public_address(),
            any_public_address(),
            option::of("[a-zA-Z][a-zA-Z0-9_]*"),
            any_data,
        )
            .prop_map(|(ident, address, local_address, name, data)| {
                let identifier = PeerId(ident);
                BondingData { identifier, address, local_address, name, data }
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

#[cfg(test)]
mod tests {
    use {
        super::compat::*, super::*, fidl_fuchsia_bluetooth_control as control,
        fidl_fuchsia_bluetooth_sys as sys,
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
            address: Address::Public([0, 0, 0, 0, 0, 0]),
            local_address: Address::Public([0, 0, 0, 0, 0, 0]),
            name: Some("name".into()),
            data: OneOrBoth::Both(
                LeData {
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
                BredrData {
                    role_preference: Some(bt::ConnectionRole::Leader),
                    services: vec![],
                    link_key: Some(remote_key.clone()),
                },
            ),
        }
    }

    #[test]
    fn host_data_to_control() {
        let host_data = HostData {
            irk: Some(sys::Key { value: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16] }),
        };
        let expected = control::HostData {
            irk: Some(Box::new(control::LocalKey {
                value: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
            })),
        };
        assert_eq!(expected, host_data.into());
    }

    #[test]
    fn empty_host_data_to_control() {
        let host_data = HostData { irk: None };
        let expected = control::HostData { irk: None };
        assert_eq!(expected, host_data.into());
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

    // Tests for conversions from fuchsia.bluetooth.sys API
    mod from_sys {
        use super::*;

        fn empty_data() -> sys::BondingData {
            sys::BondingData {
                identifier: None,
                address: None,
                local_address: None,
                name: None,
                le: None,
                bredr: None,
            }
        }

        fn empty_bredr_data() -> sys::BredrData {
            sys::BredrData { address: None, role_preference: None, link_key: None, services: None }
        }

        fn empty_le_data() -> sys::LeData {
            sys::LeData {
                address: None,
                services: None,
                connection_parameters: None,
                ltk: None,
                peer_ltk: None,
                local_ltk: None,
                irk: None,
                csrk: None,
            }
        }

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

        #[test]
        fn id_missing() {
            let src = sys::BondingData {
                identifier: None,
                address: Some(bt::Address {
                    type_: bt::AddressType::Random,
                    bytes: [1, 2, 3, 4, 5, 6],
                }),
                le: Some(empty_le_data()),
                ..empty_data()
            };
            let result = BondingData::try_from(src);
            assert_eq!(
                Err("identifier missing".to_string()),
                result.map_err(|e| format!("{:?}", e))
            );
        }

        #[test]
        fn address_missing_le() {
            let src = sys::BondingData {
                identifier: Some(bt::PeerId { value: 1 }),
                le: Some(empty_le_data()),
                ..empty_data()
            };
            let result = BondingData::try_from(src);
            assert_eq!(Err("address missing".to_string()), result.map_err(|e| format!("{:?}", e)));
        }

        #[test]
        fn address_missing_bredr() {
            let src = sys::BondingData {
                identifier: Some(bt::PeerId { value: 1 }),
                bredr: Some(empty_bredr_data()),
                ..empty_data()
            };
            let result = BondingData::try_from(src);
            assert_eq!(Err("address missing".to_string()), result.map_err(|e| format!("{:?}", e)));
        }

        #[test]
        fn address_missing_dual_mode() {
            let src = sys::BondingData {
                identifier: Some(bt::PeerId { value: 1 }),
                le: Some(empty_le_data()),
                bredr: Some(empty_bredr_data()),
                ..empty_data()
            };
            let result = BondingData::try_from(src);
            assert_eq!(Err("address missing".to_string()), result.map_err(|e| format!("{:?}", e)));
        }

        #[test]
        fn dual_mode_address_mismatch() {
            let src = sys::BondingData {
                identifier: Some(bt::PeerId { value: 1 }),
                le: Some(sys::LeData {
                    address: Some(bt::Address {
                        type_: bt::AddressType::Public,
                        bytes: [1, 2, 3, 4, 5, 6],
                    }),
                    ..empty_le_data()
                }),
                bredr: Some(sys::BredrData {
                    address: Some(bt::Address {
                        type_: bt::AddressType::Public,
                        bytes: [6, 5, 4, 3, 2, 1],
                    }),
                    ..empty_bredr_data()
                }),
                ..empty_data()
            };
            let result = BondingData::try_from(src);
            assert_eq!(
                Err("LE and BR/EDR addresses do not match".to_string()),
                result.map_err(|e| format!("{:?}", e))
            );
        }

        #[test]
        fn default_to_transport_address_le() {
            let addr = bt::Address { type_: bt::AddressType::Public, bytes: [1, 2, 3, 4, 5, 6] };
            let src = sys::BondingData {
                identifier: Some(bt::PeerId { value: 1 }),
                local_address: Some(bt::Address {
                    type_: bt::AddressType::Public,
                    bytes: [1, 0, 0, 0, 0, 0],
                }),
                le: Some(sys::LeData { address: Some(addr.clone()), ..empty_le_data() }),
                ..empty_data()
            };
            let result =
                BondingData::try_from(src).expect("failed to convert from sys.BondingData");
            assert_eq!(result.address, addr.into());
        }

        #[test]
        fn default_to_transport_address_bredr() {
            let addr = bt::Address { type_: bt::AddressType::Public, bytes: [1, 2, 3, 4, 5, 6] };
            let src = sys::BondingData {
                identifier: Some(bt::PeerId { value: 1 }),
                local_address: Some(bt::Address {
                    type_: bt::AddressType::Public,
                    bytes: [1, 0, 0, 0, 0, 0],
                }),
                bredr: Some(sys::BredrData { address: Some(addr.clone()), ..empty_bredr_data() }),
                ..empty_data()
            };
            let result =
                BondingData::try_from(src).expect("failed to convert from sys.BondingData");
            assert_eq!(result.address, addr.into());
        }

        #[test]
        fn default_to_transport_address_dual_mode() {
            let addr = bt::Address { type_: bt::AddressType::Public, bytes: [1, 2, 3, 4, 5, 6] };
            let src = sys::BondingData {
                identifier: Some(bt::PeerId { value: 1 }),
                local_address: Some(bt::Address {
                    type_: bt::AddressType::Public,
                    bytes: [1, 0, 0, 0, 0, 0],
                }),
                le: Some(sys::LeData { address: Some(addr.clone()), ..empty_le_data() }),
                bredr: Some(sys::BredrData { address: Some(addr.clone()), ..empty_bredr_data() }),
                ..empty_data()
            };
            let result =
                BondingData::try_from(src).expect("failed to convert from sys.BondingData");
            assert_eq!(result.address, addr.into());
        }

        #[test]
        fn use_top_level_address() {
            let addr = bt::Address { type_: bt::AddressType::Public, bytes: [1, 2, 3, 4, 5, 6] };
            let src = sys::BondingData {
                identifier: Some(bt::PeerId { value: 1 }),
                address: Some(addr.clone()),
                local_address: Some(bt::Address {
                    type_: bt::AddressType::Public,
                    bytes: [1, 0, 0, 0, 0, 0],
                }),
                le: Some(sys::LeData { ..empty_le_data() }),
                bredr: Some(sys::BredrData { ..empty_bredr_data() }),
                ..empty_data()
            };
            let result =
                BondingData::try_from(src).expect("failed to convert from sys.BondingData");
            assert_eq!(result.address, addr.into());
        }

        #[test]
        fn use_top_level_address_when_transport_address_present() {
            let addr = bt::Address { type_: bt::AddressType::Public, bytes: [1, 2, 3, 4, 5, 6] };
            // Assign a different transport address. This should not get used.
            let transport_addr =
                bt::Address { type_: bt::AddressType::Public, bytes: [6, 5, 4, 3, 2, 1] };
            let src = sys::BondingData {
                identifier: Some(bt::PeerId { value: 1 }),
                address: Some(addr.clone()),
                local_address: Some(bt::Address {
                    type_: bt::AddressType::Public,
                    bytes: [1, 0, 0, 0, 0, 0],
                }),
                le: Some(sys::LeData { address: Some(transport_addr.clone()), ..empty_le_data() }),
                bredr: Some(sys::BredrData { address: Some(transport_addr), ..empty_bredr_data() }),
                ..empty_data()
            };
            let result =
                BondingData::try_from(src).expect("failed to convert from sys.BondingData");
            assert_eq!(result.address, addr.into());
        }

        #[test]
        fn use_deprecated_ltk() {
            let ltk = default_ltk();
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
                le: Some(sys::LeData { ltk: Some(ltk.clone()), ..empty_le_data() }),
                bredr: Some(sys::BredrData { ..empty_bredr_data() }),
                ..empty_data()
            };

            let result =
                BondingData::try_from(src).expect("failed to convert from sys.BondingData");
            let result_le = result.le().expect("expected LE data");
            assert_eq!(result_le.peer_ltk, Some(ltk));
            assert_eq!(result_le.local_ltk, Some(ltk));
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
                le: Some(sys::LeData {
                    ltk: Some(ltk1),
                    peer_ltk: Some(ltk2.clone()),
                    local_ltk: None,
                    ..empty_le_data()
                }),
                bredr: Some(sys::BredrData { ..empty_bredr_data() }),
                ..empty_data()
            };

            let result =
                BondingData::try_from(src).expect("failed to convert from sys.BondingData");
            let result_le = result.le().expect("expected LE data");
            assert_eq!(result_le.peer_ltk, Some(ltk2));
            assert_eq!(result_le.local_ltk, None);
        }
    }

    // Tests for conversions from fuchsia.bluetooth.control API
    mod from_control {
        use super::*;

        fn default_data() -> control::BondingData {
            control::BondingData {
                identifier: "".to_string(),
                local_address: "".to_string(),
                name: None,
                le: None,
                bredr: None,
            }
        }

        fn default_le_data() -> control::LeData {
            control::LeData {
                address: "".to_string(),
                address_type: control::AddressType::LePublic,
                connection_parameters: None,
                services: vec![],
                ltk: None,
                irk: None,
                csrk: None,
            }
        }

        fn default_bredr_data() -> control::BredrData {
            control::BredrData {
                address: "".to_string(),
                piconet_leader: true,
                services: vec![],
                link_key: None,
            }
        }

        #[test]
        fn id_malformed() {
            let src = control::BondingData { identifier: "derp".to_string(), ..default_data() };
            let result = BondingData::try_from(src);
            assert!(result.is_err());
        }

        #[test]
        fn le_address_malformed() {
            let src = control::BondingData {
                identifier: "123".to_string(),
                local_address: "00:00:00:00:00:00".to_string(),
                le: Some(Box::new(control::LeData {
                    address: "derp".to_string(),
                    ..default_le_data()
                })),
                ..default_data()
            };
            let result = BondingData::try_from(src);
            assert!(result.is_err());
        }

        #[test]
        fn bredr_address_malformed() {
            let src = control::BondingData {
                identifier: "123".to_string(),
                local_address: "00:00:00:00:00:00".to_string(),
                bredr: Some(Box::new(control::BredrData {
                    address: "derp".to_string(),
                    ..default_bredr_data()
                })),
                ..default_data()
            };
            let result = BondingData::try_from(src);
            assert!(result.is_err());
        }

        #[test]
        fn dual_mode_le_address_malformed() {
            let src = control::BondingData {
                identifier: "123".to_string(),
                local_address: "00:00:00:00:00:00".to_string(),
                le: Some(Box::new(control::LeData {
                    address: "derp".to_string(),
                    ..default_le_data()
                })),
                bredr: Some(Box::new(control::BredrData {
                    address: "00:00:00:00:00:01".to_string(),
                    ..default_bredr_data()
                })),
                ..default_data()
            };
            let result = BondingData::try_from(src);
            assert!(result.is_err());
        }

        #[test]
        fn dual_mode_bredr_address_malformed() {
            let src = control::BondingData {
                identifier: "123".to_string(),
                local_address: "00:00:00:00:00:00".to_string(),
                le: Some(Box::new(control::LeData {
                    address: "00:00:00:00:00:01".to_string(),
                    ..default_le_data()
                })),
                bredr: Some(Box::new(control::BredrData {
                    address: "derp".to_string(),
                    ..default_bredr_data()
                })),
                ..default_data()
            };
            let result = BondingData::try_from(src);
            assert!(result.is_err());
        }

        #[test]
        fn dual_mode_address_mismatch() {
            let src = control::BondingData {
                identifier: "123".to_string(),
                local_address: "00:00:00:00:00:00".to_string(),
                le: Some(Box::new(control::LeData {
                    address: "00:00:00:00:00:01".to_string(),
                    ..default_le_data()
                })),
                bredr: Some(Box::new(control::BredrData {
                    address: "00:00:00:00:00:02".to_string(),
                    ..default_bredr_data()
                })),
                ..default_data()
            };
            let result = BondingData::try_from(src);
            assert!(result.is_err());
        }

        #[test]
        fn dual_mode_address_type_mismatch() {
            let src = control::BondingData {
                identifier: "123".to_string(),
                local_address: "00:00:00:00:00:00".to_string(),
                le: Some(Box::new(control::LeData {
                    address: "00:00:00:00:00:01".to_string(),
                    address_type: control::AddressType::LeRandom,
                    ..default_le_data()
                })),
                bredr: Some(Box::new(control::BredrData {
                    address: "00:00:00:00:00:01".to_string(),
                    ..default_bredr_data()
                })),
                ..default_data()
            };
            let result = BondingData::try_from(src);
            assert!(result.is_err());
        }

        #[test]
        fn use_le_address() {
            let addr = "00:00:00:00:00:01".to_string();
            let src = control::BondingData {
                identifier: "123".to_string(),
                local_address: "00:00:00:00:00:00".to_string(),
                le: Some(Box::new(control::LeData {
                    address: addr.clone(),
                    address_type: control::AddressType::LeRandom,
                    ..default_le_data()
                })),
                ..default_data()
            };
            let result =
                BondingData::try_from(src).expect("failed to convert from control.BondingData");
            let addr = Address::random_from_str(&addr).expect("failed to convert address");
            assert_eq!(result.address, addr);
        }

        #[test]
        fn use_bredr_address() {
            let addr = "00:00:00:00:00:01".to_string();
            let src = control::BondingData {
                identifier: "123".to_string(),
                local_address: "00:00:00:00:00:00".to_string(),
                bredr: Some(Box::new(control::BredrData {
                    address: addr.clone(),
                    ..default_bredr_data()
                })),
                ..default_data()
            };
            let result =
                BondingData::try_from(src).expect("failed to convert from control.BondingData");
            let addr = Address::public_from_str(&addr).expect("failed to convert address");
            assert_eq!(result.address, addr);
        }

        #[test]
        fn dual_mode_address() {
            let addr = "00:00:00:00:00:01".to_string();
            let src = control::BondingData {
                identifier: "123".to_string(),
                local_address: "00:00:00:00:00:00".to_string(),
                le: Some(Box::new(control::LeData { address: addr.clone(), ..default_le_data() })),
                bredr: Some(Box::new(control::BredrData {
                    address: addr.clone(),
                    ..default_bredr_data()
                })),
                ..default_data()
            };
            let result =
                BondingData::try_from(src).expect("failed to convert from control.BondingData");
            let addr = Address::public_from_str(&addr).expect("failed to convert address");
            assert_eq!(result.address, addr);
        }
    }

    // The test cases below use proptest to exercise round-trip conversions between FIDL and the
    // library type across several permutations.
    mod roundtrip {
        use super::{
            proptest_util::{
                any_bonding_data, any_bonding_data_for_control_test, any_bredr_data, any_le_data,
                any_le_data_for_control_test,
            },
            *,
        };
        use crate::types::address::proptest_util::any_public_address;
        use proptest::prelude::*;

        proptest! {
            #[test]
            fn bredr_data_sys_roundtrip(data in any_bredr_data()) {
                let sys_bredr_data: sys::BredrData = data.clone().into();
                assert_eq!(Ok(data), sys_bredr_data.try_into().map_err(|e: anyhow::Error| e.to_string()));
            }
            #[test]
            fn bredr_data_control_roundtrip((address, data) in (any_public_address(), any_bredr_data())) {
                let control_bredr_data = BredrData::into_control(data.clone(), &address);
                assert_eq!(address.to_string(), control_bredr_data.address);
                assert_eq!(Ok(data), control_bredr_data.try_into().map_err(|e: anyhow::Error| e.to_string()));
            }

            #[test]
            fn le_data_sys_roundtrip(data in any_le_data()) {
                let sys_le_data: sys::LeData = data.clone().into();
                assert_eq!(Ok(data), sys_le_data.try_into().map_err(|e: anyhow::Error| e.to_string()));
            }
            #[test]
            fn le_data_control_roundtrip((address, mut data) in (any_public_address(), any_le_data_for_control_test())) {
                let control_le_data = LeData::into_control(data.clone(), &address);
                assert_eq!(address.to_string(), control_le_data.address);
                assert_eq!(Ok(data), control_le_data.try_into().map_err(|e: anyhow::Error| e.to_string()));
            }
            #[test]
            fn bonding_data_sys_roundtrip(data in any_bonding_data()) {
                let peer_id = data.identifier;
                let sys_bonding_data: sys::BondingData = data.clone().into();
                assert_eq!(Ok(data), (sys_bonding_data, peer_id).try_into().map_err(|e: anyhow::Error| e.to_string()));
            }
            #[test]
            fn bonding_data_control_roundtrip(data in any_bonding_data_for_control_test()) {
                let control_bonding_data: control::BondingData = data.clone().into();
                assert_eq!(Ok(data), control_bonding_data.try_into().map_err(|e: anyhow::Error| e.to_string()));
            }
        }
    }
}
