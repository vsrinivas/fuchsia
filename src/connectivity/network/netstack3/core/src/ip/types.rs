// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::fmt::Debug;

use net_types::ip::{IpAddr, IpAddress, Ipv4Addr, Ipv6Addr, Subnet, SubnetEither};
use net_types::SpecifiedAddr;

/// The destination for forwarding a packet.
///
/// `EntryDest` can either be a device or another network address.
#[allow(missing_docs)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum EntryDest<A, D> {
    Local { device: D },
    Remote { next_hop: SpecifiedAddr<A> },
}

/// A local forwarding destination, or a remote forwarding destination that can
/// be an IPv4 or an IPv6 address.
pub type EntryDestEither<D> = EntryDest<IpAddr, D>;

impl<A: IpAddress, D> EntryDest<A, D>
where
    SpecifiedAddr<IpAddr>: From<SpecifiedAddr<A>>,
{
    fn into_ip_addr(self) -> EntryDest<IpAddr, D> {
        match self {
            EntryDest::Local { device } => EntryDest::Local { device },
            EntryDest::Remote { next_hop } => EntryDest::Remote { next_hop: next_hop.into() },
        }
    }
}

/// A forwarding entry.
///
/// `Entry` is a `Subnet` paired with an `EntryDest`.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct Entry<A: IpAddress, D> {
    pub subnet: Subnet<A>,
    pub dest: EntryDest<A, D>,
}

/// An IPv4 forwarding entry or an IPv6 forwarding entry.
#[allow(missing_docs)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum EntryEither<D> {
    V4(Entry<Ipv4Addr, D>),
    V6(Entry<Ipv6Addr, D>),
}

impl<D> EntryEither<D> {
    /// Creates a new [`EntryEither`] with the given `subnet` and `destination`.
    ///
    /// Returns `None` if `subnet` and `destination` are not the same IP version
    /// (both `V4` or both `V6`) when `destination` is a remote value.
    pub fn new(subnet: SubnetEither, destination: EntryDestEither<D>) -> Option<EntryEither<D>> {
        match destination {
            EntryDest::Local { device } => match subnet {
                SubnetEither::V4(subnet) => {
                    Some(EntryEither::V4(Entry { subnet, dest: EntryDest::Local { device } }))
                }
                SubnetEither::V6(subnet) => {
                    Some(EntryEither::V6(Entry { subnet, dest: EntryDest::Local { device } }))
                }
            },
            EntryDest::Remote { next_hop } => match (subnet, next_hop.into()) {
                (SubnetEither::V4(subnet), IpAddr::V4(next_hop)) => {
                    Some(EntryEither::V4(Entry { subnet, dest: EntryDest::Remote { next_hop } }))
                }
                (SubnetEither::V6(subnet), IpAddr::V6(next_hop)) => {
                    Some(EntryEither::V6(Entry { subnet, dest: EntryDest::Remote { next_hop } }))
                }
                _ => None,
            },
        }
    }

    /// Gets the subnet and destination for this [`EntryEither`].
    pub fn into_subnet_dest(self) -> (SubnetEither, EntryDestEither<D>) {
        match self {
            EntryEither::V4(entry) => (entry.subnet.into(), entry.dest.into_ip_addr()),
            EntryEither::V6(entry) => (entry.subnet.into(), entry.dest.into_ip_addr()),
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
        let entry_v4: EntryDest<_, ()> = EntryDest::Remote {
            next_hop: SpecifiedAddr::new(Ipv4Addr::new([192, 168, 0, 1])).unwrap(),
        }
        .into_ip_addr();
        let entry_v6: EntryDest<_, ()> = EntryDest::Remote {
            next_hop: SpecifiedAddr::new(Ipv6Addr::from_bytes([
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
            ]))
            .unwrap(),
        }
        .into_ip_addr();
        assert_eq!(EntryEither::new(subnet_v4, entry_v6), None);
        assert_eq!(EntryEither::new(subnet_v6, entry_v4), None);
        let valid_v4 = EntryEither::new(subnet_v4, entry_v4).unwrap();
        let valid_v6 = EntryEither::new(subnet_v6, entry_v6).unwrap();
        // Check that the split produces results requal to the generating parts.
        assert_eq!((subnet_v4, entry_v4), valid_v4.into_subnet_dest());
        assert_eq!((subnet_v6, entry_v6), valid_v6.into_subnet_dest());
    }
}
