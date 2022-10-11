// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! IP forwarding definitions.

use alloc::vec::Vec;
use core::{fmt::Debug, slice::Iter};

use log::debug;
use net_types::{
    ip::{Ip, IpAddress, Subnet},
    SpecifiedAddr,
};
use thiserror::Error;

use crate::ip;

// TODO(joshlf):
// - How do we detect circular routes? Do we attempt to detect at rule
//   installation time? At runtime? Using what algorithm?

/// The destination of an outbound IP packet.
///
/// Outbound IP packets are sent to a particular device (specified by the
/// `device` field). They are sent to a particular IP host on the local network
/// attached to that device, identified by `next_hop`. Note that `next_hop` is
/// not necessarily the destination IP address of the IP packet. In particular,
/// if the destination is not on the local network, the `next_hop` will be the
/// IP address of the next IP router on the way to the destination.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub(crate) struct Destination<A: IpAddress, D> {
    pub(crate) next_hop: SpecifiedAddr<A>,
    pub(crate) device: D,
}

/// An error encountered when adding a forwarding entry.
#[derive(Error, Debug, PartialEq)]
pub enum AddRouteError {
    /// Indicates that the route already exists.
    #[error("Already exists")]
    AlreadyExists,

    /// Indicates the gateway is not a neighbor of the host.
    #[error("Gateway is not a neighbor")]
    GatewayNotNeighbor,
}

impl From<crate::error::ExistsError> for AddRouteError {
    fn from(crate::error::ExistsError: crate::error::ExistsError) -> AddRouteError {
        AddRouteError::AlreadyExists
    }
}

/// An IP forwarding table.
///
/// `ForwardingTable` maps destination subnets to the nearest IP hosts (on the
/// local network) able to route IP packets to those subnets.
// TODO(ghanan): Use metrics to determine active route?
pub struct ForwardingTable<I: Ip, D> {
    /// All the routes available to forward a packet.
    ///
    /// `table` may have redundant, but unique, paths to the same
    /// destination.
    ///
    /// The entries are sorted based on the subnet's prefix length and
    /// local-ness; when there's a tie in prefix length, on-linkness breaks the
    /// tie (with the on-link routes appearing before off-link ones).
    table: Vec<ip::types::Entry<I::Addr, D>>,
}

impl<I: Ip, D> Default for ForwardingTable<I, D> {
    fn default() -> ForwardingTable<I, D> {
        ForwardingTable { table: Vec::new() }
    }
}

impl<I: Ip, D: Clone + Debug + PartialEq> ForwardingTable<I, D> {
    /// Adds `entry` to the forwarding table if it does not already exist.
    fn add_entry(
        &mut self,
        entry: ip::types::Entry<I::Addr, D>,
    ) -> Result<(), crate::error::ExistsError> {
        let Self { table } = self;

        if table.contains(&entry) {
            // If we already have this exact route, don't add it again.
            return Err(crate::error::ExistsError);
        }

        // Insert the new entry after the last route to a more specific and/or
        // local subnet to maintain the invariant that the table is sorted by
        // subnet prefix length and local-ness.
        let ip::types::Entry { subnet, device: _, gateway: _ } = entry;
        let prefix = subnet.prefix();
        table.insert(
            table.partition_point(|ip::types::Entry { subnet, device: _, gateway }| {
                let subnet_prefix = subnet.prefix();
                subnet_prefix > prefix
                    || (subnet_prefix == prefix
                        && match gateway {
                            Some(SpecifiedAddr { .. }) => false,
                            // on-link routes have a higher preference.
                            None => true,
                        })
            }),
            entry,
        );

        Ok(())
    }

    // TODO(joshlf): Should `next_hop` actually be restricted even further,
    // perhaps to unicast addresses?

    /// Add a route to a destination subnet that requires going through a
    /// gateway.
    ///
    /// The egress device for the gateway is calculated before inserting the
    /// route to the forwarding table. Note that the gateway must be a neighbor.
    pub(crate) fn add_route(
        &mut self,
        subnet: Subnet<I::Addr>,
        gateway: SpecifiedAddr<I::Addr>,
    ) -> Result<(), AddRouteError> {
        debug!("adding route: {} -> {}", subnet, gateway);

        let device = self.lookup(None, gateway).map_or(
            Err(AddRouteError::GatewayNotNeighbor),
            |Destination { next_hop, device }| {
                // If the gateway is a neighbor, the next hop to the gateway
                // should be the gateway itself.
                if next_hop != gateway {
                    Err(AddRouteError::GatewayNotNeighbor)
                } else {
                    Ok(device)
                }
            },
        )?;

        self.add_entry(ip::types::Entry { subnet, device, gateway: Some(gateway) })
            .map_err(From::from)
    }

    /// Add a route to a destination subnet that lives on a link an interface is
    /// attached to.
    pub(crate) fn add_device_route(
        &mut self,
        subnet: Subnet<I::Addr>,
        device: D,
    ) -> Result<(), crate::error::ExistsError> {
        debug!("adding device route: {} -> {:?}", subnet, device);
        self.add_entry(ip::types::Entry { subnet, device, gateway: None })
    }

    /// Delete all routes to a subnet, returning `Err` if no route was found to
    /// be deleted.
    ///
    /// Returns all the deleted entries on success.
    ///
    /// Note, `del_route` will remove *all* routes to a `subnet`, including
    /// routes that consider `subnet` on-link for some device and routes that
    /// require packets destined to a node within `subnet` to be routed through
    /// some next-hop node.
    pub(crate) fn del_route(
        &mut self,
        subnet: Subnet<I::Addr>,
    ) -> Result<Vec<ip::types::Entry<I::Addr, D>>, crate::error::NotFoundError> {
        debug!("deleting route: {}", subnet);

        // Delete all routes to a subnet.
        //
        // TODO(https://github.com/rust-lang/rust/issues/43244): Use
        // drain_filter to avoid extra allocation.
        let Self { table } = self;
        let owned_table = core::mem::replace(table, Vec::new());
        let (owned_table, removed) =
            owned_table.into_iter().partition(|entry| entry.subnet != subnet);
        *table = owned_table;
        if removed.is_empty() {
            // If a path to `subnet` was not in our installed table, then it
            // definitely won't be in our active routes cache.
            return Err(crate::error::NotFoundError);
        }

        Ok(removed)
    }

    /// Get an iterator over all of the forwarding entries ([`Entry`]) this
    /// `ForwardingTable` knows about.
    pub(crate) fn iter_table(&self) -> Iter<'_, ip::types::Entry<I::Addr, D>> {
        self.table.iter()
    }

    /// Look up an address in the table.
    ///
    /// Look up an IP address in the table, returning a next hop IP address and
    /// a device to send over. If `address` is link-local, then the returned
    /// next hop will be `address`. Otherwise, it will be the link-local address
    /// of an IP router capable of delivering packets to `address`.
    ///
    /// If `device` is specified, the available routes are limited to those that
    /// egress over the device.
    ///
    /// If `address` matches an entry which maps to an IP address, `lookup` will
    /// look that address up in the table as well, continuing until a link-local
    /// address and device are found.
    ///
    /// If multiple entries match `address` or any intermediate IP address, the
    /// entry with the longest prefix will be chosen.
    ///
    /// The unspecified address (0.0.0.0 in IPv4 and :: in IPv6) are not
    /// routable and will return None even if they have been added to the table.
    pub(crate) fn lookup(
        &self,
        local_device: Option<&D>,
        address: SpecifiedAddr<I::Addr>,
    ) -> Option<Destination<I::Addr, D>> {
        let Self { table } = self;

        // Get all potential routes we could take to reach `address`.
        table.iter().find_map(|ip::types::Entry { subnet, device, gateway }| {
            (subnet.contains(&address) && local_device.map_or(true, |d| d == device)).then(|| {
                let next_hop =
                    if let Some(next_hop) = gateway { next_hop.clone() } else { address };

                Destination { next_hop, device: device.clone() }
            })
        })
    }

    /// Retains only the entries that pass the predicate.
    pub(crate) fn retain<F: FnMut(&ip::types::Entry<I::Addr, D>) -> bool>(&mut self, pred: F) {
        self.table.retain(pred)
    }
}

#[cfg(test)]
mod tests {
    use fakealloc::collections::HashSet;
    use ip_test_macro::ip_test;
    use log::trace;
    use net_types::ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};

    use super::*;
    use crate::{device::DeviceId, testutil::DummyEventDispatcherConfig};

    impl<I: Ip, D: Clone + Debug + PartialEq> ForwardingTable<I, D> {
        /// Print the table.
        fn print_table(&self) {
            trace!("Installed Routing table:");

            if self.table.is_empty() {
                trace!("    No Routes");
                return;
            }

            for ip::types::Entry { subnet, device, gateway } in self.iter_table() {
                trace!("    {} -> via {:?} on device {:?}", subnet, gateway, device);
            }
        }
    }

    trait TestIpExt: crate::testutil::TestIpExt {
        fn subnet(v: u8, neg_prefix: u8) -> Subnet<Self::Addr>;

        fn next_hop_addr_sub(
            v: u8,
            neg_prefix: u8,
        ) -> (SpecifiedAddr<Self::Addr>, Subnet<Self::Addr>);

        fn next_hop_addr() -> SpecifiedAddr<Self::Addr>;
    }

    impl TestIpExt for Ipv4 {
        fn subnet(v: u8, neg_prefix: u8) -> Subnet<Ipv4Addr> {
            Subnet::new(Ipv4Addr::new([v, 0, 0, 0]), 32 - neg_prefix).unwrap()
        }

        fn next_hop_addr_sub(v: u8, neg_prefix: u8) -> (SpecifiedAddr<Ipv4Addr>, Subnet<Ipv4Addr>) {
            (SpecifiedAddr::new(Ipv4Addr::new([v, 0, 0, 1])).unwrap(), Ipv4::subnet(v, neg_prefix))
        }

        fn next_hop_addr() -> SpecifiedAddr<Ipv4Addr> {
            SpecifiedAddr::new(Ipv4Addr::new([10, 0, 0, 1])).unwrap()
        }
    }

    impl TestIpExt for Ipv6 {
        fn subnet(v: u8, neg_prefix: u8) -> Subnet<Ipv6Addr> {
            Subnet::new(
                Ipv6Addr::from([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, v, 0, 0, 0]),
                128 - neg_prefix,
            )
            .unwrap()
        }

        fn next_hop_addr_sub(v: u8, neg_prefix: u8) -> (SpecifiedAddr<Ipv6Addr>, Subnet<Ipv6Addr>) {
            (
                SpecifiedAddr::new(Ipv6Addr::from([
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, v, 0, 0, 1,
                ]))
                .unwrap(),
                Ipv6::subnet(v, neg_prefix),
            )
        }

        fn next_hop_addr() -> SpecifiedAddr<Ipv6Addr> {
            SpecifiedAddr::new(Ipv6Addr::from([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 1]))
                .unwrap()
        }
    }

    fn simple_setup<I: Ip + TestIpExt>() -> (
        ForwardingTable<I, DeviceId>,
        DummyEventDispatcherConfig<I::Addr>,
        SpecifiedAddr<I::Addr>,
        Subnet<I::Addr>,
        DeviceId,
    ) {
        let mut table = ForwardingTable::<I, DeviceId>::default();

        let config = I::DUMMY_CONFIG;
        let subnet = config.subnet;
        let device = DeviceId::new_ethernet(0);
        let (next_hop, next_hop_subnet) = I::next_hop_addr_sub(1, 1);

        // Should add the route successfully.
        table.add_device_route(subnet, device.clone()).unwrap();
        assert_eq!(
            table.iter_table().collect::<Vec<_>>(),
            &[&ip::types::Entry { subnet, device: device.clone(), gateway: None }]
        );

        // Attempting to add the route again should fail.
        assert_eq!(
            table.add_device_route(subnet, device.clone()).unwrap_err(),
            crate::error::ExistsError
        );
        assert_eq!(
            table.iter_table().collect::<Vec<_>>(),
            &[&ip::types::Entry { subnet, device: device.clone(), gateway: None }]
        );

        // Add the route but as a next hop route.
        table.add_device_route(next_hop_subnet, device.clone()).unwrap();
        table.add_route(subnet, next_hop).unwrap();
        assert_eq!(
            table.iter_table().collect::<HashSet<_>>(),
            HashSet::from([
                &ip::types::Entry { subnet, device: device.clone(), gateway: None },
                &ip::types::Entry {
                    subnet: next_hop_subnet,
                    device: device.clone(),
                    gateway: None
                },
                &ip::types::Entry { subnet, device: device.clone(), gateway: Some(next_hop) },
            ])
        );

        // Attempting to add the route again should fail.
        assert_eq!(table.add_route(subnet, next_hop).unwrap_err(), AddRouteError::AlreadyExists);
        assert_eq!(
            table.iter_table().collect::<HashSet<_>>(),
            HashSet::from([
                &ip::types::Entry { subnet, device: device.clone(), gateway: None },
                &ip::types::Entry {
                    subnet: next_hop_subnet,
                    device: device.clone(),
                    gateway: None
                },
                &ip::types::Entry { subnet, device: device.clone(), gateway: Some(next_hop) },
            ])
        );

        (table, config, next_hop, next_hop_subnet, device)
    }

    #[ip_test]
    fn test_simple_add_del<I: Ip + TestIpExt>() {
        let (mut table, config, next_hop, next_hop_subnet, device) = simple_setup::<I>();
        assert_eq!(table.iter_table().count(), 3);

        // Delete all routes to subnet.
        assert_eq!(
            table.del_route(config.subnet).unwrap().into_iter().collect::<HashSet<_>>(),
            HashSet::from([
                ip::types::Entry { subnet: config.subnet, device: device.clone(), gateway: None },
                ip::types::Entry {
                    subnet: config.subnet,
                    device: device.clone(),
                    gateway: Some(next_hop)
                }
            ])
        );

        assert_eq!(
            table.iter_table().collect::<Vec<_>>(),
            &[&ip::types::Entry { subnet: next_hop_subnet, device: device.clone(), gateway: None }]
        );
    }

    #[ip_test]
    fn test_simple_lookup<I: Ip + TestIpExt>() {
        let (mut table, config, next_hop, _next_hop_subnet, device) = simple_setup::<I>();

        // Do lookup for our next hop (should be the device).
        assert_eq!(
            table.lookup(None, next_hop),
            Some(Destination { next_hop, device: device.clone() })
        );

        // Do lookup for some address within `subnet`.
        assert_eq!(
            table.lookup(None, config.local_ip),
            Some(Destination { next_hop: config.local_ip, device: device.clone() })
        );
        assert_eq!(
            table.lookup(None, config.remote_ip),
            Some(Destination { next_hop: config.remote_ip, device: device.clone() })
        );

        // Delete routes to the subnet and make sure that we can no longer route
        // to destinations in the subnet.
        assert_eq!(
            table.del_route(config.subnet).unwrap().into_iter().collect::<HashSet<_>>(),
            HashSet::from([
                ip::types::Entry { subnet: config.subnet, device: device.clone(), gateway: None },
                ip::types::Entry {
                    subnet: config.subnet,
                    device: device.clone(),
                    gateway: Some(next_hop)
                }
            ])
        );
        assert_eq!(
            table.lookup(None, next_hop),
            Some(Destination { next_hop, device: device.clone() })
        );
        assert_eq!(table.lookup(None, config.local_ip), None);
        assert_eq!(table.lookup(None, config.remote_ip), None);

        // Make the subnet routable again but through a gateway.
        table.add_route(config.subnet, next_hop).unwrap();
        assert_eq!(
            table.lookup(None, next_hop),
            Some(Destination { next_hop, device: device.clone() })
        );
        assert_eq!(
            table.lookup(None, config.local_ip),
            Some(Destination { next_hop, device: device.clone() })
        );
        assert_eq!(
            table.lookup(None, config.remote_ip),
            Some(Destination { next_hop, device: device.clone() })
        );
    }

    #[ip_test]
    fn test_default_route_ip<I: Ip + TestIpExt>() {
        let mut table = ForwardingTable::<I, DeviceId>::default();
        let device0 = DeviceId::new_ethernet(0);
        let (addr1, sub1) = I::next_hop_addr_sub(1, 24);
        let (addr2, _) = I::next_hop_addr_sub(2, 24);
        let (addr3, _) = I::next_hop_addr_sub(3, 24);

        // Add the following routes:
        //  sub1 -> device0
        //
        // Our expected forwarding table should look like:
        //  sub1 -> device0

        table.add_device_route(sub1, device0.clone()).unwrap();
        table.print_table();
        assert_eq!(
            table.lookup(None, addr1).unwrap(),
            Destination { next_hop: addr1, device: device0.clone() }
        );
        assert_eq!(table.lookup(None, addr2), None);

        // Add a default route.
        //
        // Our expected forwarding table should look like:
        //  sub1 -> device0
        //  default -> addr1 w/ device0

        let default_sub = Subnet::new(I::UNSPECIFIED_ADDRESS, 0).unwrap();
        table.add_route(default_sub, addr1).unwrap();
        assert_eq!(
            table.lookup(None, addr1).unwrap(),
            Destination { next_hop: addr1, device: device0.clone() }
        );
        assert_eq!(
            table.lookup(None, addr2).unwrap(),
            Destination { next_hop: addr1, device: device0.clone() }
        );
        assert_eq!(
            table.lookup(None, addr3).unwrap(),
            Destination { next_hop: addr1, device: device0.clone() }
        );
    }

    #[ip_test]
    fn test_device_filter_with_varying_prefix_lengths<I: Ip + TestIpExt>() {
        const MORE_SPECIFIC_SUB_DEVICE: u8 = 1;
        const LESS_SPECIFIC_SUB_DEVICE: u8 = 2;

        let mut table = ForwardingTable::<I, u8>::default();
        let (next_hop, more_specific_sub) = I::next_hop_addr_sub(1, 1);
        let less_specific_sub = {
            let (addr, sub) = I::next_hop_addr_sub(1, 2);
            assert_eq!(next_hop, addr);
            sub
        };

        table.add_device_route(less_specific_sub, LESS_SPECIFIC_SUB_DEVICE.clone()).unwrap();
        assert_eq!(
            table.lookup(None, next_hop),
            Some(Destination { next_hop, device: LESS_SPECIFIC_SUB_DEVICE.clone() }),
            "matches route"
        );
        assert_eq!(
            table.lookup(Some(&LESS_SPECIFIC_SUB_DEVICE), next_hop),
            Some(Destination { next_hop, device: LESS_SPECIFIC_SUB_DEVICE.clone() }),
            "route matches specified device"
        );
        assert_eq!(
            table.lookup(Some(&MORE_SPECIFIC_SUB_DEVICE), next_hop),
            None,
            "no route with the specified device"
        );

        table.add_device_route(more_specific_sub, MORE_SPECIFIC_SUB_DEVICE.clone()).unwrap();
        assert_eq!(
            table.lookup(None, next_hop).unwrap(),
            Destination { next_hop, device: MORE_SPECIFIC_SUB_DEVICE.clone() },
            "matches most specific route"
        );
        assert_eq!(
            table.lookup(Some(&LESS_SPECIFIC_SUB_DEVICE), next_hop),
            Some(Destination { next_hop, device: LESS_SPECIFIC_SUB_DEVICE.clone() }),
            "matches less specific route with the specified device"
        );
        assert_eq!(
            table.lookup(Some(&MORE_SPECIFIC_SUB_DEVICE), next_hop).unwrap(),
            Destination { next_hop, device: MORE_SPECIFIC_SUB_DEVICE.clone() },
            "matches the most specific route with the specified device"
        );
    }

    #[ip_test]
    fn test_multiple_routes_to_subnet_through_different_devices<I: Ip + TestIpExt>() {
        const DEVICE1: u8 = 1;
        const DEVICE2: u8 = 2;

        let mut table = ForwardingTable::<I, u8>::default();
        let (next_hop, sub) = I::next_hop_addr_sub(1, 1);

        table.add_device_route(sub, DEVICE1.clone()).unwrap();
        table.add_device_route(sub, DEVICE2.clone()).unwrap();
        let lookup = table.lookup(None, next_hop);
        assert!(
            [
                Some(Destination { next_hop, device: DEVICE1.clone() }),
                Some(Destination { next_hop, device: DEVICE2.clone() })
            ]
            .contains(&lookup),
            "lookup = {:?}",
            lookup
        );
        assert_eq!(
            table.lookup(Some(&DEVICE1), next_hop),
            Some(Destination { next_hop, device: DEVICE1.clone() }),
        );
        assert_eq!(
            table.lookup(Some(&DEVICE2), next_hop),
            Some(Destination { next_hop, device: DEVICE2.clone() }),
        );
    }
}
