// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::enums::*;
use super::{Correlated, CorrelatedBox, NetFlags, RouteFlags};
use anyhow::{format_err, Context as _};
use core::convert::{TryFrom, TryInto};
use fidl_fuchsia_lowpan_device::{AllCounters, MacCounters};
use spinel_pack::*;
use std::collections::HashSet;
use std::hash::Hash;
use std::io;
use std::num::NonZeroU8;

/// A type for decoding/encoding the Spinel header byte.
#[derive(Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct Header(u8);

impl Header {
    /// Creates a new instance of `Header` based on the given NLI and TID.
    ///
    /// Valid values for NLI are between 0 and 3, inclusive.
    /// Valid values for TID are `None` or between `Some(1)` and `Some(15)`, inclusive.
    ///
    /// If either the NLI and/or TID are invalid, then
    /// this constructor will return `None`.
    pub fn new(nli: u8, tid: Option<NonZeroU8>) -> Option<Header> {
        let tid = tid.map_or(0, NonZeroU8::get);

        if nli < 4 && tid < 16 {
            Some(Header(0x80 + (nli << 4) + tid))
        } else {
            None
        }
    }

    /// Network link ID.
    pub fn nli(&self) -> u8 {
        (self.0 & 0x30) >> 4
    }

    /// Transaction Id
    pub fn tid(&self) -> Option<NonZeroU8> {
        NonZeroU8::new(self.0 & 0x0F)
    }
}

impl std::fmt::Debug for Header {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Header").field("nli", &self.nli()).field("tid", &self.tid()).finish()
    }
}

impl From<Header> for u8 {
    fn from(header: Header) -> Self {
        header.0
    }
}

impl TryFrom<u8> for Header {
    type Error = anyhow::Error;
    fn try_from(x: u8) -> Result<Self, Self::Error> {
        if x & 0xC0 != 0x80 {
            Err(format_err!("Bad header byte"))?;
        }
        Ok(Header(x))
    }
}

impl TryPackAs<u8> for Header {
    fn pack_as_len(&self) -> std::io::Result<usize> {
        Ok(1)
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
        TryPackAs::<u8>::try_pack_as(&self.0, buffer)
    }
}

impl TryPack for Header {
    fn pack_len(&self) -> io::Result<usize> {
        Ok(1)
    }

    fn try_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        TryPackAs::<u8>::try_pack_as(&self.0, buffer)
    }
}

impl<'a> TryUnpackAs<'a, u8> for Header {
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        let id: u8 = TryUnpackAs::<u8>::try_unpack_as(iter)?;

        id.try_into().context(UnpackingError::InvalidValue)
    }
}

impl<'a> TryUnpack<'a> for Header {
    type Unpacked = Header;
    fn try_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
        TryUnpackAs::<u8>::try_unpack_as(iter)
    }
}

/// Struct that represents a decoded Spinel command frame with a
/// borrowed reference to the payload.
#[spinel_packed("CiD")]
#[derive(Debug, Clone, Copy)]
pub struct SpinelFrameRef<'a> {
    pub header: Header,
    pub cmd: Cmd,
    pub payload: &'a [u8],
}

/// Struct that represents a decoded Spinel property payload with a
/// borrowed reference to the value.
#[spinel_packed("iD")]
#[derive(Debug, Clone, Copy)]
pub struct SpinelPropValueRef<'a> {
    pub prop: Prop,
    pub value: &'a [u8],
}

/// Spinel Protocol Version struct, for determining device compatibility.
#[spinel_packed("ii")]
#[derive(Debug, Clone, Copy)]
pub struct ProtocolVersion(pub u32, pub u32);

/// The MAC section of a spinel network scan result
#[spinel_packed("ESSC")]
#[derive(Debug)]
pub struct NetScanResultMac {
    pub long_addr: EUI64,
    pub short_addr: u16,
    pub panid: u16,
    pub lqi: u8,
}

/// The NET section of a spinel network scan result
#[spinel_packed("iCUdd")]
#[derive(Debug)]
pub struct NetScanResultNet {
    pub net_type: u32,
    pub flags: u8,
    pub network_name: Vec<u8>,
    pub xpanid: Vec<u8>,
    pub steering_data: Vec<u8>,
}

/// A spinel network scan result
#[spinel_packed("Ccdd")]
#[derive(Debug)]
pub struct NetScanResult {
    pub channel: u8,
    pub rssi: i8,
    pub mac: NetScanResultMac,
    pub net: NetScanResultNet,
}

/// A spinel energy scan result
#[spinel_packed("Cc")]
#[derive(Debug)]
pub struct EnergyScanResult {
    pub channel: u8,
    pub rssi: i8,
}

/// A spinel IPv6 subnet
#[spinel_packed("6C")]
#[derive(Hash, Clone, Eq, PartialEq)]
pub struct Subnet {
    pub addr: std::net::Ipv6Addr,
    pub prefix_len: u8,
}

impl std::fmt::Debug for Subnet {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}/{}", self.addr, self.prefix_len)
    }
}

impl std::fmt::Display for Subnet {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}/{}", self.addr, self.prefix_len)
    }
}

impl TryFrom<fidl_fuchsia_net::Subnet> for Subnet {
    type Error = anyhow::Error;
    fn try_from(x: fidl_fuchsia_net::Subnet) -> Result<Self, Self::Error> {
        match x {
            fidl_fuchsia_net::Subnet {
                addr: fidl_fuchsia_net::IpAddress::Ipv6(addr),
                prefix_len,
            } => Ok(Subnet { addr: addr.addr.into(), prefix_len }),
            _ => Err(format_err!("Cannot convert IPv4 subnet into IPv6 subnet")),
        }
    }
}

impl Into<fidl_fuchsia_lowpan::Ipv6Subnet> for Subnet {
    fn into(self) -> fidl_fuchsia_lowpan::Ipv6Subnet {
        fidl_fuchsia_lowpan::Ipv6Subnet {
            addr: fidl_fuchsia_net::Ipv6Address { addr: self.addr.octets() },
            prefix_len: self.prefix_len,
        }
    }
}

impl From<fidl_fuchsia_lowpan::Ipv6Subnet> for Subnet {
    fn from(subnet: fidl_fuchsia_lowpan::Ipv6Subnet) -> Self {
        Self { addr: subnet.addr.addr.into(), prefix_len: subnet.prefix_len }
    }
}

/// A spinel address table entry from SPINEL_PROP_IPV6_ADDRESS_TABLE
#[spinel_packed("DLL")]
#[derive(Clone, Eq)]
pub struct AddressTableEntry {
    pub subnet: Subnet,
    pub preferred_lifetime: u32,
    pub valid_lifetime: u32,
}

pub type AddressTable = HashSet<AddressTableEntry>;

impl std::convert::From<Subnet> for AddressTableEntry {
    fn from(subnet: Subnet) -> Self {
        AddressTableEntry {
            subnet,
            preferred_lifetime: std::u32::MAX,
            valid_lifetime: std::u32::MAX,
        }
    }
}

impl std::fmt::Debug for AddressTableEntry {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.subnet)?;
        match (self.preferred_lifetime != 0, self.valid_lifetime != 0) {
            (true, true) => Ok(()),
            (false, true) => write!(f, " DEPRECATED"),
            (_, false) => write!(f, " INVALID"),
        }
    }
}

impl std::fmt::Display for AddressTableEntry {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.subnet)?;
        match (self.preferred_lifetime != 0, self.valid_lifetime != 0) {
            (true, true) => Ok(()),
            (false, true) => write!(f, " DEPRECATED"),
            (_, false) => write!(f, " INVALID"),
        }
    }
}

impl Default for AddressTableEntry {
    fn default() -> Self {
        AddressTableEntry {
            subnet: Subnet { addr: std::net::Ipv6Addr::UNSPECIFIED, prefix_len: 0 },
            preferred_lifetime: std::u32::MAX,
            valid_lifetime: std::u32::MAX,
        }
    }
}

impl PartialEq for AddressTableEntry {
    fn eq(&self, other: &Self) -> bool {
        self.subnet == other.subnet
    }
}

impl std::hash::Hash for AddressTableEntry {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.subnet.hash(state);
    }
}

/// A on-mesh network entry from SPINEL_PROP_THREAD_ON_MESH_NETS
#[spinel_packed("DbCbS")]
#[derive(Clone, Eq, PartialEq, Hash)]
pub struct OnMeshNet {
    pub subnet: Subnet,
    pub stable: bool,
    pub flags: NetFlags,
    pub local: bool,
    pub rloc16: u16,
}

impl Into<fidl_fuchsia_lowpan_device::OnMeshPrefix> for OnMeshNet {
    fn into(self) -> fidl_fuchsia_lowpan_device::OnMeshPrefix {
        fidl_fuchsia_lowpan_device::OnMeshPrefix {
            subnet: Some(self.subnet.into()),
            default_route_preference: self.flags.route_preference(),
            stable: Some(self.stable),
            slaac_preferred: Some(self.flags.is_slaac_preferred()),
            slaac_valid: Some(self.flags.is_slaac_valid()),
            ..fidl_fuchsia_lowpan_device::OnMeshPrefix::EMPTY
        }
    }
}

impl From<fidl_fuchsia_lowpan_device::OnMeshPrefix> for OnMeshNet {
    fn from(prefix: fidl_fuchsia_lowpan_device::OnMeshPrefix) -> Self {
        use fidl_fuchsia_lowpan_device::RoutePreference;
        Self {
            subnet: prefix.subnet.expect("OnMeshNet missing required field `subnet`").into(),
            stable: prefix.stable.unwrap_or(false),
            flags: {
                let mut flags = NetFlags::default();
                if prefix.slaac_preferred == Some(true) {
                    flags |= NetFlags::SLAAC_PREFERRED;
                }
                if prefix.slaac_valid == Some(true) {
                    flags |= NetFlags::SLAAC_VALID;
                }
                if let Some(pref) = prefix.default_route_preference {
                    flags |= NetFlags::DEFAULT_ROUTE;
                    match pref {
                        RoutePreference::High => flags |= NetFlags::PREF_HIGH,
                        RoutePreference::Medium => flags |= NetFlags::PREF_MED,
                        RoutePreference::Low => flags |= NetFlags::PREF_LOW,
                    }
                }
                flags
            },
            local: true,
            rloc16: Default::default(),
        }
    }
}

pub type OnMeshNets = HashSet<CorrelatedBox<OnMeshNet>>;

impl Correlated for OnMeshNet {
    fn correlation_hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.subnet.hash(state);
        self.local.hash(state);
        if !self.local {
            self.rloc16.hash(state);
        }
    }
    fn correlation_eq(&self, other: &Self) -> bool {
        (self.subnet == other.subnet)
            && (self.local == other.local)
            && (self.local || self.rloc16 == other.rloc16)
    }
}

impl std::fmt::Debug for OnMeshNet {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} ", self.subnet)?;

        if self.local {
            write!(f, "LOCAL")?;
        } else {
            write!(f, "@{:04X}", self.rloc16)?;
        }

        if self.stable {
            write!(f, " STABLE")?;
        }

        write!(f, " {:?}", self.flags)
    }
}

/// A external route entry from SPINEL_PROP_THREAD_OFF_MESH_ROUTES
#[spinel_packed("DbCbbS")]
#[derive(Clone, Eq, PartialEq, Hash)]
pub struct ExternalRoute {
    pub subnet: Subnet,
    pub stable: bool,
    pub flags: RouteFlags,
    pub local: bool,
    pub next_hop: bool,
    pub rloc16: u16,
}

impl Into<fidl_fuchsia_lowpan_device::ExternalRoute> for ExternalRoute {
    fn into(self) -> fidl_fuchsia_lowpan_device::ExternalRoute {
        fidl_fuchsia_lowpan_device::ExternalRoute {
            subnet: Some(self.subnet.into()),
            route_preference: Some(self.flags.route_preference()),
            stable: Some(self.stable),
            ..fidl_fuchsia_lowpan_device::ExternalRoute::EMPTY
        }
    }
}

impl From<fidl_fuchsia_lowpan_device::ExternalRoute> for ExternalRoute {
    fn from(route: fidl_fuchsia_lowpan_device::ExternalRoute) -> Self {
        use fidl_fuchsia_lowpan_device::RoutePreference;
        Self {
            subnet: route.subnet.expect("OnMeshNet missing required field `subnet`").into(),
            stable: route.stable.unwrap_or(false),
            flags: match route.route_preference {
                Some(RoutePreference::High) => RouteFlags::PREF_HIGH,
                Some(RoutePreference::Low) => RouteFlags::PREF_LOW,
                _ => RouteFlags::PREF_MED,
            },
            local: true,
            next_hop: true,
            rloc16: Default::default(),
        }
    }
}

pub type ExternalRoutes = HashSet<CorrelatedBox<ExternalRoute>>;

impl Correlated for ExternalRoute {
    fn correlation_hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.subnet.hash(state);
        self.local.hash(state);
        if !self.local {
            self.rloc16.hash(state);
        }
    }
    fn correlation_eq(&self, other: &Self) -> bool {
        (self.subnet == other.subnet)
            && (self.local == other.local)
            && (self.local || self.rloc16 == other.rloc16)
    }
}

impl std::fmt::Debug for ExternalRoute {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} ", self.subnet)?;

        if self.local {
            write!(f, "LOCAL")?;
        } else {
            write!(f, "@{:04X}", self.rloc16)?;
        }

        if self.stable {
            write!(f, " STABLE")?;
        }

        if self.next_hop {
            write!(f, " NEXT_HOP")?;
        }

        write!(f, " {:?}", self.flags)
    }
}

/// A spinel network packet
#[spinel_packed("dD")]
#[derive(Debug, Hash, Clone, Eq, PartialEq)]
pub struct NetworkPacket<'a> {
    pub packet: &'a [u8],
    pub metadata: &'a [u8],
}

/// An allow list entry
#[spinel_packed("Ec")]
#[derive(Debug, Hash, Clone, Eq, PartialEq)]
pub struct AllowListEntry {
    pub mac_addr: EUI64,
    pub rssi: i8,
}

pub type AllowList = HashSet<AllowListEntry>;

/// An deny list entry
#[spinel_packed("E")]
#[derive(Debug, Hash, Clone, Eq, PartialEq)]
pub struct DenyListEntry {
    pub mac_addr: EUI64,
}

pub type DenyList = HashSet<DenyListEntry>;

#[spinel_packed("ESLCcCbLLc")]
#[derive(Debug, Hash, Clone, Eq, PartialEq)]
pub struct NeighborTableEntry {
    pub extended_addr: EUI64,
    pub short_addr: u16,
    pub age: u32,
    pub link_quality: u8,
    pub avg_rssi: i8,
    pub mode: u8,
    pub is_child: bool,
    pub link_frame_cnt: u32,
    pub mle_frame_cnt: u32,
    pub last_rssi: i8,
}

pub type NeighborTable = Vec<NeighborTableEntry>;

#[spinel_packed("dd")]
#[derive(Debug, Hash, Clone, Eq, PartialEq)]
pub struct AllMacCounters {
    pub tx_counters: Vec<u32>,
    pub rx_counters: Vec<u32>,
}

impl std::convert::Into<AllCounters> for AllMacCounters {
    fn into(self) -> AllCounters {
        AllCounters {
            mac_tx: Some(MacCounters {
                total: Some(self.tx_counters[0]),
                unicast: Some(self.tx_counters[1]),
                broadcast: Some(self.tx_counters[2]),
                ack_requested: Some(self.tx_counters[3]),
                acked: Some(self.tx_counters[4]),
                no_ack_requested: Some(self.tx_counters[5]),
                data: Some(self.tx_counters[6]),
                data_poll: Some(self.tx_counters[7]),
                beacon: Some(self.tx_counters[8]),
                beacon_request: Some(self.tx_counters[9]),
                other: Some(self.tx_counters[10]),
                retries: Some(self.tx_counters[11]),
                direct_max_retry_expiry: Some(self.tx_counters[15]),
                indirect_max_retry_expiry: Some(self.tx_counters[16]),
                err_cca: Some(self.tx_counters[12]),
                err_abort: Some(self.tx_counters[13]),
                err_busy_channel: Some(self.tx_counters[14]),
                ..MacCounters::EMPTY
            }),
            mac_rx: Some(MacCounters {
                total: Some(self.rx_counters[0]),
                unicast: Some(self.rx_counters[1]),
                broadcast: Some(self.rx_counters[2]),
                data: Some(self.rx_counters[3]),
                data_poll: Some(self.rx_counters[4]),
                beacon: Some(self.rx_counters[5]),
                beacon_request: Some(self.rx_counters[6]),
                other: Some(self.rx_counters[7]),
                address_filtered: Some(self.rx_counters[8]),
                dest_addr_filtered: Some(self.rx_counters[9]),
                duplicated: Some(self.rx_counters[10]),
                err_no_frame: Some(self.rx_counters[11]),
                err_unknown_neighbor: Some(self.rx_counters[12]),
                err_invalid_src_addr: Some(self.rx_counters[13]),
                err_sec: Some(self.rx_counters[14]),
                err_fcs: Some(self.rx_counters[15]),
                err_other: Some(self.rx_counters[16]),
                ..MacCounters::EMPTY
            }),
            ..AllCounters::EMPTY
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;

    #[test]
    fn test_header() {
        assert_eq!(Header::new(0, None), Some(Header(0x80)));
        assert_eq!(Header::new(3, None), Some(Header(0xb0)));
        assert_eq!(Header::new(4, None), None);
        assert_eq!(Header::new(0, NonZeroU8::new(1)), Some(Header(0x81)));
        assert_eq!(Header::new(0, NonZeroU8::new(15)), Some(Header(0x8F)));
        assert_eq!(Header::new(0, NonZeroU8::new(16)), None);

        assert_eq!(Header::new(3, NonZeroU8::new(15)), Some(Header(0xBF)));
        assert_eq!(Header::new(4, NonZeroU8::new(16)), None);

        assert_matches!(Header::try_from(0x00), Err(_));
        assert_matches!(Header::try_from(0xBF), Ok(Header(0xBF)));
        assert_matches!(Header::try_from(0xFF), Err(_));

        assert_eq!(Header(0x80).tid(), None);
        assert_eq!(Header(0x80).nli(), 0);
        assert_eq!(Header(0xBF).tid(), NonZeroU8::new(15));
        assert_eq!(Header(0xBF).nli(), 3);
    }

    #[test]
    fn test_on_mesh_nets_unpack() {
        let data: &[u8] = &[
            0x16, 0x00, 0xfd, 0x00, 0xab, 0xcd, 0x12, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x40, 0x01, 0x31, 0x00, 0x01, 0x30, 0x16, 0x00, 0xfd, 0x00,
            0x12, 0x23, 0xab, 0xcd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x40, 0x01, 0x31, 0x00, 0x01, 0x30,
        ];
        println!(
            "on_mesh_nets_unpack: {:#?}",
            OnMeshNets::try_unpack_from_slice(data).expect("unable to unpack OnMeshNets")
        );
    }

    #[test]
    fn test_on_mesh_net_unpack() {
        let data: &[u8] = &[
            0xfd, 0x00, 0xab, 0xcd, 0x12, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x40, 0x01, 0x31, 0x00, 0x01, 0x30,
        ];
        println!(
            "on_mesh_net_unpack: {:#?}",
            OnMeshNet::try_unpack_from_slice(data).expect("unable to unpack OnMeshNet")
        );
    }

    #[test]
    fn test_external_routes_unpack() {
        let data: &[u8] = &[
            0x17, 0x00, 0xfd, 0x00, 0xab, 0xcd, 0x12, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x40, 0x01, 0x31, 0x00, 0x00, 0x01, 0x30, 0x17, 0x00, 0xfd,
            0x00, 0x12, 0x23, 0xab, 0xcd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x40, 0x01, 0x31, 0x00, 0x00, 0x01, 0x30,
        ];
        println!(
            "external_routes_unpack: {:#?}",
            ExternalRoutes::try_unpack_from_slice(data).expect("unable to unpack ExternalRoutes")
        );
    }

    #[test]
    fn test_external_route_unpack() {
        let data: &[u8] = &[
            0xfd, 0x00, 0xab, 0xcd, 0x12, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x40, 0x01, 0x31, 0x00, 0x00, 0x01, 0x30,
        ];
        println!(
            "external_route_unpack: {:#?}",
            ExternalRoute::try_unpack_from_slice(data).expect("unable to unpack ExternalRoute")
        );
    }
}
