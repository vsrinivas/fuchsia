// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Rather than reuse existing _ext types, we define intermediary types for
//! JSON serialization to avoid coupling too closely to particular FIDL
//! protocols.

use itertools::Itertools as _;

enum SubnetEnum {
    V4(Subnet<std::net::Ipv4Addr>),
    V6(Subnet<std::net::Ipv6Addr>),
}

impl From<fidl_fuchsia_net_ext::Subnet> for SubnetEnum {
    fn from(ext: fidl_fuchsia_net_ext::Subnet) -> SubnetEnum {
        let fidl_fuchsia_net_ext::Subnet {
            addr: fidl_fuchsia_net_ext::IpAddress(addr),
            prefix_len,
        } = ext;
        match addr {
            std::net::IpAddr::V4(addr) => SubnetEnum::V4(Subnet { addr, prefix_len }),
            std::net::IpAddr::V6(addr) => SubnetEnum::V6(Subnet { addr, prefix_len }),
        }
    }
}

#[derive(serde::Serialize)]
struct Subnet<T = std::net::IpAddr> {
    addr: T,
    prefix_len: u8,
}

impl From<fidl_fuchsia_net_ext::Subnet> for Subnet<std::net::IpAddr> {
    fn from(ext: fidl_fuchsia_net_ext::Subnet) -> Subnet<std::net::IpAddr> {
        let fidl_fuchsia_net_ext::Subnet {
            addr: fidl_fuchsia_net_ext::IpAddress(addr),
            prefix_len,
        } = ext;
        Subnet { addr, prefix_len }
    }
}

#[derive(serde::Serialize)]
struct Addresses {
    ipv4: Vec<Subnet<std::net::Ipv4Addr>>,
    ipv6: Vec<Subnet<std::net::Ipv6Addr>>,
}

impl From<Vec<fidl_fuchsia_net_ext::Subnet>> for Addresses {
    fn from(addresses: Vec<fidl_fuchsia_net_ext::Subnet>) -> Addresses {
        let (ipv4, ipv6): (Vec<_>, Vec<_>) = addresses
            .into_iter()
            .map(SubnetEnum::from)
            .partition_map(|subnet_enum| match subnet_enum {
                SubnetEnum::V4(subnet) => itertools::Either::Left(subnet),
                SubnetEnum::V6(subnet) => itertools::Either::Right(subnet),
            });
        Addresses { ipv4, ipv6 }
    }
}

#[derive(serde::Serialize)]
struct Features {
    wlan: bool,
    synthetic: bool,
    loopback: bool,
}

impl From<fidl_fuchsia_hardware_ethernet::Features> for Features {
    fn from(features: fidl_fuchsia_hardware_ethernet::Features) -> Features {
        Features {
            wlan: features.contains(fidl_fuchsia_hardware_ethernet::Features::Wlan),
            synthetic: features.contains(fidl_fuchsia_hardware_ethernet::Features::Synthetic),
            loopback: features.contains(fidl_fuchsia_hardware_ethernet::Features::Loopback),
        }
    }
}

// Allow dead code for use in static assertion.
#[allow(dead_code)]
const fn all_known_features() -> u32 {
    // It would be better not to use bits(), but the `|` operator on Features
    // is not const.
    fidl_fuchsia_hardware_ethernet::Features::Wlan.bits()
        | fidl_fuchsia_hardware_ethernet::Features::Synthetic.bits()
        | fidl_fuchsia_hardware_ethernet::Features::Loopback.bits()
}

// Assert that we exhaust every Feature bitflag in the above struct.
static_assertions::const_assert_eq!(
    all_known_features(),
    fidl_fuchsia_hardware_ethernet::Features::all().bits(),
);

#[derive(serde::Serialize)]
/// Intermediary struct for serializing InterfaceProperties into JSON.
pub struct InterfaceView {
    nicid: u64,
    name: String,
    topopath: String,
    filepath: String,
    mac: Option<fidl_fuchsia_net_ext::MacAddress>,
    mtu: u32,
    features: Features,
    admin_up: bool,
    link_up: bool,
    addresses: Addresses,
}

impl From<fidl_fuchsia_net_stack_ext::InterfaceInfo> for InterfaceView {
    fn from(info: fidl_fuchsia_net_stack_ext::InterfaceInfo) -> InterfaceView {
        let fidl_fuchsia_net_stack_ext::InterfaceInfo {
            id,
            properties:
                fidl_fuchsia_net_stack_ext::InterfaceProperties {
                    name,
                    topopath,
                    filepath,
                    mac,
                    mtu,
                    features,
                    administrative_status,
                    physical_status,
                    addresses,
                },
        } = info;
        InterfaceView {
            nicid: id,
            name,
            topopath,
            filepath,
            mac: mac.map(|fidl_fuchsia_hardware_ethernet_ext::MacAddress { octets }| {
                fidl_fuchsia_net_ext::MacAddress { octets }
            }),
            mtu,
            features: features.into(),
            admin_up: match administrative_status {
                fidl_fuchsia_net_stack_ext::AdministrativeStatus::ENABLED => true,
                fidl_fuchsia_net_stack_ext::AdministrativeStatus::DISABLED => false,
            },
            link_up: match physical_status {
                fidl_fuchsia_net_stack_ext::PhysicalStatus::UP => true,
                fidl_fuchsia_net_stack_ext::PhysicalStatus::DOWN => false,
            },
            addresses: addresses.into(),
        }
    }
}

#[derive(serde::Serialize)]
/// Intermediary struct for serializing IP forwarding table entries into JSON.
pub struct ForwardingEntry {
    subnet: Subnet,
    destination: ForwardingDestination,
}

impl From<fidl_fuchsia_net_stack_ext::ForwardingEntry> for ForwardingEntry {
    fn from(
        fidl_fuchsia_net_stack_ext::ForwardingEntry { subnet, destination }: fidl_fuchsia_net_stack_ext::ForwardingEntry,
    ) -> ForwardingEntry {
        ForwardingEntry { subnet: subnet.into(), destination: destination.into() }
    }
}

#[derive(serde::Serialize)]
enum ForwardingDestination {
    DeviceId(u64),
    NextHop(std::net::IpAddr),
}

impl From<fidl_fuchsia_net_stack_ext::ForwardingDestination> for ForwardingDestination {
    fn from(dest: fidl_fuchsia_net_stack_ext::ForwardingDestination) -> ForwardingDestination {
        match dest {
            fidl_fuchsia_net_stack_ext::ForwardingDestination::DeviceId(id) => {
                ForwardingDestination::DeviceId(id)
            }
            fidl_fuchsia_net_stack_ext::ForwardingDestination::NextHop(
                fidl_fuchsia_net_ext::IpAddress(ip),
            ) => ForwardingDestination::NextHop(ip),
        }
    }
}

#[derive(serde::Serialize)]
/// Intermediary struct for serializing route table entries into JSON.
pub struct RouteTableEntry {
    destination: Subnet,
    gateway: Option<std::net::IpAddr>,
    nicid: u64,
    metric: u32,
}

impl From<fidl_fuchsia_netstack_ext::RouteTableEntry> for RouteTableEntry {
    fn from(
        fidl_fuchsia_netstack_ext::RouteTableEntry {
        destination,
        gateway,
        nicid,
        metric,
     }: fidl_fuchsia_netstack_ext::RouteTableEntry,
    ) -> RouteTableEntry {
        RouteTableEntry {
            destination: destination.into(),
            gateway: gateway.map(|fidl_fuchsia_net_ext::IpAddress(addr)| addr),
            nicid: nicid.into(),
            metric,
        }
    }
}

pub struct NeighborTableEntryIteratorItemVariants<T> {
    existing: T,
    added: T,
    changed: T,
    removed: T,
    idle: T,
}

impl<T> NeighborTableEntryIteratorItemVariants<T> {
    pub fn select(self, item: &fidl_fuchsia_net_neighbor::EntryIteratorItem) -> T {
        use fidl_fuchsia_net_neighbor::EntryIteratorItem;
        let Self { existing, added, changed, removed, idle } = self;
        match item {
            EntryIteratorItem::Existing(_) => existing,
            EntryIteratorItem::Added(_) => added,
            EntryIteratorItem::Changed(_) => changed,
            EntryIteratorItem::Removed(_) => removed,
            EntryIteratorItem::Idle(_) => idle,
        }
    }
}

impl<T> IntoIterator for NeighborTableEntryIteratorItemVariants<T> {
    type Item = T;
    type IntoIter = <[T; 5] as IntoIterator>::IntoIter;

    fn into_iter(self) -> Self::IntoIter {
        let Self { existing, added, changed, removed, idle } = self;
        IntoIterator::into_iter([existing, added, changed, removed, idle])
    }
}

pub const DISPLAYED_NEIGH_ENTRY_VARIANTS: NeighborTableEntryIteratorItemVariants<&'static str> =
    NeighborTableEntryIteratorItemVariants {
        existing: "EXISTING",
        added: "ADDED",
        changed: "CHANGED",
        removed: "REMOVED",
        idle: "IDLE",
    };

/// Intermediary type for serializing Entry (e.g. into JSON).
#[derive(serde::Serialize)]
pub struct NeighborTableEntry {
    interface: Option<u64>,
    neighbor: Option<std::net::IpAddr>,
    state: &'static str,
    mac: Option<fidl_fuchsia_net_ext::MacAddress>,
}

impl From<fidl_fuchsia_net_neighbor_ext::Entry> for NeighborTableEntry {
    fn from(
        fidl_fuchsia_net_neighbor_ext::Entry(fidl_fuchsia_net_neighbor::Entry {
            interface,
            neighbor,
            state,
            mac,
            // Ignored since the tabular format ignores this field.
            updated_at: _,
            ..
        }): fidl_fuchsia_net_neighbor_ext::Entry,
    ) -> NeighborTableEntry {
        NeighborTableEntry {
            interface,
            neighbor: neighbor.map(|i| {
                let fidl_fuchsia_net_ext::IpAddress(addr) = i.into();
                addr
            }),
            state: fidl_fuchsia_net_neighbor_ext::display_entry_state(&state),
            mac: mac.map(|mac| mac.into()),
        }
    }
}
