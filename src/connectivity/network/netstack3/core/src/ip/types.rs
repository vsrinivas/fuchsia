// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::fmt::Debug;

use net_types::{
    ip::{IpAddr, IpAddress, Ipv4Addr, Ipv6Addr, Subnet, SubnetEither},
    SpecifiedAddr,
};

/// `AddableEntry` is a routing entry that may be used to add a new entry to the
/// forwarding table.
///
/// See [`Entry`] for the type used to represent a route in the forwarding
/// table.
///
/// `AddableEntry` guarantees that at least one of the egress device or
/// gateway is set.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub struct AddableEntry<A: IpAddress, D> {
    subnet: Subnet<A>,
    device: Option<D>,
    gateway: Option<SpecifiedAddr<A>>,
}

/// An IPv4 forwarding entry or an IPv6 forwarding entry.
#[allow(missing_docs)]
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub enum AddableEntryEither<D> {
    V4(AddableEntry<Ipv4Addr, D>),
    V6(AddableEntry<Ipv6Addr, D>),
}

impl<D> AddableEntryEither<D> {
    /// Creates a new [`AddableEntryEither`].
    ///
    /// Returns `None` if `subnet` and `destination` are not the same IP version
    /// (both `V4` or both `V6`) when `destination` is a remote value, or if
    /// both device and gateway are `None`.
    pub fn new(
        subnet: SubnetEither,
        device: Option<D>,
        gateway: Option<IpAddr<SpecifiedAddr<Ipv4Addr>, SpecifiedAddr<Ipv6Addr>>>,
    ) -> Option<AddableEntryEither<D>> {
        // TODO(https://fxbug.dev/96721): Reduce complexity of the invariants
        // upheld here.
        match (subnet, device, gateway) {
            (SubnetEither::V4(subnet), device, Some(IpAddr::V4(gateway))) => {
                Some(AddableEntryEither::V4(AddableEntry {
                    subnet,
                    device,
                    gateway: Some(gateway),
                }))
            }
            (SubnetEither::V6(subnet), device, Some(IpAddr::V6(gateway))) => {
                Some(AddableEntryEither::V6(AddableEntry {
                    subnet,
                    device,
                    gateway: Some(gateway),
                }))
            }
            (SubnetEither::V4(subnet), Some(device), None) => {
                Some(AddableEntryEither::V4(AddableEntry {
                    subnet,
                    device: Some(device),
                    gateway: None,
                }))
            }
            (SubnetEither::V6(subnet), Some(device), None) => {
                Some(AddableEntryEither::V6(AddableEntry {
                    subnet,
                    device: Some(device),
                    gateway: None,
                }))
            }
            (SubnetEither::V4(_), None, Some(IpAddr::V6(_)))
            | (SubnetEither::V4(_), Some(_), Some(IpAddr::V6(_)))
            | (SubnetEither::V4(_), None, None)
            | (SubnetEither::V6(_), None, Some(IpAddr::V4(_)))
            | (SubnetEither::V6(_), Some(_), Some(IpAddr::V4(_)))
            | (SubnetEither::V6(_), None, None) => None,
        }
    }

    /// Gets the subnet, egress device and gateway.
    pub fn into_subnet_device_gateway(
        self,
    ) -> (SubnetEither, Option<D>, Option<IpAddr<SpecifiedAddr<Ipv4Addr>, SpecifiedAddr<Ipv6Addr>>>)
    {
        match self {
            AddableEntryEither::V4(AddableEntry { subnet, device, gateway }) => {
                (subnet.into(), device, gateway.map(|a| a.into()))
            }
            AddableEntryEither::V6(AddableEntry { subnet, device, gateway }) => {
                (subnet.into(), device, gateway.map(|a| a.into()))
            }
        }
    }
}

impl<D> From<AddableEntry<Ipv4Addr, D>> for AddableEntryEither<D> {
    fn from(entry: AddableEntry<Ipv4Addr, D>) -> AddableEntryEither<D> {
        AddableEntryEither::V4(entry)
    }
}

impl<D> From<AddableEntry<Ipv6Addr, D>> for AddableEntryEither<D> {
    fn from(entry: AddableEntry<Ipv6Addr, D>) -> AddableEntryEither<D> {
        AddableEntryEither::V6(entry)
    }
}

/// A forwarding entry.
///
/// `Entry` is a `Subnet` with an egress device and optional gateway.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub struct Entry<A: IpAddress, D> {
    pub subnet: Subnet<A>,
    pub device: D,
    pub gateway: Option<SpecifiedAddr<A>>,
}

/// An IPv4 forwarding entry or an IPv6 forwarding entry.
#[allow(missing_docs)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum EntryEither<D> {
    V4(Entry<Ipv4Addr, D>),
    V6(Entry<Ipv6Addr, D>),
}

impl<D> EntryEither<D> {
    /// Gets the subnet, egress device and gateway.
    pub fn into_subnet_device_gateway(
        self,
    ) -> (SubnetEither, D, Option<IpAddr<SpecifiedAddr<Ipv4Addr>, SpecifiedAddr<Ipv6Addr>>>) {
        match self {
            EntryEither::V4(Entry { subnet, device, gateway }) => {
                (subnet.into(), device, gateway.map(|a| a.into()))
            }
            EntryEither::V6(Entry { subnet, device, gateway }) => {
                (subnet.into(), device, gateway.map(|a| a.into()))
            }
        }
    }
}

impl<D> From<Entry<Ipv4Addr, D>> for EntryEither<D> {
    fn from(entry: Entry<Ipv4Addr, D>) -> EntryEither<D> {
        EntryEither::V4(entry)
    }
}

impl<D> From<Entry<Ipv6Addr, D>> for EntryEither<D> {
    fn from(entry: Entry<Ipv6Addr, D>) -> EntryEither<D> {
        EntryEither::V6(entry)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_entry_either() {
        // Check that trying to build an EntryEither with mismatching IpAddr
        // fails, and with matching ones succeeds.
        let subnet_v4 = Subnet::new(Ipv4Addr::new([192, 168, 0, 0]), 24).unwrap().into();
        let subnet_v6 =
            Subnet::new(Ipv6Addr::from_bytes([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]), 64)
                .unwrap()
                .into();
        let gateway_v4: IpAddr<_, _> =
            SpecifiedAddr::new(Ipv4Addr::new([192, 168, 0, 1])).unwrap().into();
        let gateway_v6: IpAddr<_, _> = SpecifiedAddr::new(Ipv6Addr::from_bytes([
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
        ]))
        .unwrap()
        .into();

        for d in [None, Some(())] {
            assert_eq!(AddableEntryEither::new(subnet_v4, d, Some(gateway_v6)), None);
            assert_eq!(AddableEntryEither::new(subnet_v6, d, Some(gateway_v4)), None);

            let valid_v4 = AddableEntryEither::new(subnet_v4, d, Some(gateway_v4)).unwrap();
            let valid_v6 = AddableEntryEither::new(subnet_v6, d, Some(gateway_v6)).unwrap();
            // Check that the split produces results equal to the generating parts.
            assert_eq!((subnet_v4, d, Some(gateway_v4)), valid_v4.into_subnet_device_gateway());
            assert_eq!((subnet_v6, d, Some(gateway_v6)), valid_v6.into_subnet_device_gateway());
        }
    }
}
