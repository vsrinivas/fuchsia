// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::{fmt::Debug, slice::Iter};

use net_types::{
    ip::{Ip, IpAddress, Subnet},
    SpecifiedAddr,
};

use crate::ip::*;

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
    /// The entries are sorted based on the subnet's prefix length - larger
    /// prefix lengths appear first.
    table: Vec<Entry<I::Addr, D>>,
}

impl<I: Ip, D> Default for ForwardingTable<I, D> {
    fn default() -> ForwardingTable<I, D> {
        ForwardingTable { table: Vec::new() }
    }
}

impl<I: Ip, D: Clone + Debug + PartialEq> ForwardingTable<I, D> {
    /// Adds `entry` to the forwarding table if it does not already exist.
    fn add_entry(&mut self, entry: Entry<I::Addr, D>) -> Result<(), ExistsError> {
        let Self { table } = self;

        if table.contains(&entry) {
            // If we already have this exact route, don't add it again.
            return Err(ExistsError);
        }

        // Insert the new entry after the last route to a more specific subnet
        // to maintain the invariant that the table is sorted by subnet prefix
        // length.
        let Entry { subnet, dest: _ } = entry;
        let prefix = subnet.prefix();
        table.insert(
            table.partition_point(|Entry { subnet, dest: _ }| subnet.prefix() > prefix),
            entry,
        );

        Ok(())
    }

    // TODO(joshlf): Should `next_hop` actually be restricted even further,
    // perhaps to unicast addresses?

    /// Add a route to a destination subnet that requires going through another
    /// node.
    pub(crate) fn add_route(
        &mut self,
        subnet: Subnet<I::Addr>,
        next_hop: SpecifiedAddr<I::Addr>,
    ) -> Result<(), ExistsError> {
        debug!("adding route: {} -> {}", subnet, next_hop);
        self.add_entry(Entry { subnet, dest: EntryDest::Remote { next_hop } })
    }

    /// Add a route to a destination subnet that lives on a link an interface is
    /// attached to.
    pub(crate) fn add_device_route(
        &mut self,
        subnet: Subnet<I::Addr>,
        device: D,
    ) -> Result<(), ExistsError> {
        debug!("adding device route: {} -> {:?}", subnet, device);
        self.add_entry(Entry { subnet, dest: EntryDest::Local { device } })
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
    ) -> Result<Vec<Entry<I::Addr, D>>, NotFoundError> {
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
            return Err(NotFoundError);
        }

        Ok(removed)
    }

    /// Get an iterator over all of the forwarding entries ([`Entry`]) this
    /// `ForwardingTable` knows about.
    pub(crate) fn iter_table(&self) -> Iter<'_, Entry<I::Addr, D>> {
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
        device: Option<D>,
        address: SpecifiedAddr<I::Addr>,
    ) -> Option<Destination<I::Addr, D>> {
        use alloc::vec;

        let mut observed = vec![false; self.table.len()];
        self.lookup_helper(device, address, &mut observed)
    }

    /// Find the destination a packet destined to `address` should be routed to
    /// by inspecting the table.
    ///
    /// `observed` will be marked with each entry we have already observed to
    /// prevent loops.
    fn lookup_helper(
        &self,
        local_device: Option<D>,
        address: SpecifiedAddr<I::Addr>,
        observed: &mut Vec<bool>,
    ) -> Option<Destination<I::Addr, D>> {
        // Get all potential routes we could take to reach `address`.
        let q = self.table.iter().enumerate().filter(|(_, Entry { subnet, dest })| {
            subnet.contains(&address)
                && match dest {
                    EntryDest::Local { device } => {
                        local_device.as_ref().map_or(true, |d| d == device)
                    }
                    EntryDest::Remote { .. } => true,
                }
        });

        // The best route to reach `address` so far.
        //
        // Tuple of (ADDRESS_PREFIX, `Destination`).
        let mut best_so_far: Option<(u8, Destination<I::Addr, D>)> = None;

        for (i, e) in q {
            // Check if we already observed this entry.
            if observed[i] {
                // If we already observed this entry, then that means we are
                // hitting it again, indicating the following:
                //  1) There is a loop.
                //  2) The last time we hit this entry, we did not find a valid
                //     destination.
                // Given what we know, we skip checking it again.
                continue;
            }

            // Mark the entry as now observed.
            observed[i] = true;

            match &e.dest {
                EntryDest::Local { device } => {
                    // If we have a best route so far and its subnet prefix is
                    // greater than the one we are looking at right now, skip.
                    // Otherwise, if the the subnet prefix is less than the one
                    // we are looking at right now, or the prefixes are equal
                    // but the existing best destination is a remote, update to
                    // the this local.
                    if let Some(best_so_far) = &mut best_so_far {
                        if best_so_far.0 > e.subnet.prefix() {
                            continue;
                        } else if best_so_far.0 < e.subnet.prefix()
                            || best_so_far.1.next_hop != address
                        {
                            // If the prefixes are equal, we know this is a
                            // remote because for local destinations, the next
                            // hop MUST match `address`.
                            *best_so_far = (
                                e.subnet.prefix(),
                                Destination { next_hop: address, device: device.clone() },
                            );
                        }
                    } else {
                        // No best route exists, this is the best so far.
                        best_so_far = Some((
                            e.subnet.prefix(),
                            Destination { next_hop: address, device: device.clone() },
                        ));
                    }
                }
                EntryDest::Remote { next_hop } => {
                    // If we have a best route so far and its subnet prefix is
                    // greater than or equal to the one we are looking at right
                    // now, skip.
                    if let Some(best_so_far) = best_so_far.clone() {
                        if best_so_far.0 >= e.subnet.prefix() {
                            continue;
                        }
                    }

                    // If the subnet requires a next hop, attempt to resolve the
                    // route to the next hop. If no route exists, ignore this
                    // potential match as we have no path to `address` with this
                    // route. If a route exists, keep it as the best route so
                    // far.
                    if let Some(dest) =
                        self.lookup_helper(local_device.clone(), *next_hop, observed)
                    {
                        best_so_far = Some((e.subnet.prefix(), dest));
                    }
                }
            }
        }

        // Return the best destination we know about.
        best_so_far.map(|x| x.1)
    }
}

#[cfg(test)]
mod tests {
    use fakealloc::collections::HashSet;
    use specialize_ip_macro::ip_test;

    use super::*;
    use crate::{device::DeviceId, testutil::assert_empty};

    impl<I: Ip, D: Clone + Debug + PartialEq> ForwardingTable<I, D> {
        /// Print the table.
        fn print_table(&self) {
            trace!("Installed Routing table:");

            if self.table.is_empty() {
                trace!("    No Routes");
                return;
            }

            for e in self.iter_table() {
                trace!("    {} -> {:?} ", e.subnet, e.dest);
            }
        }

        /// Delete the route to a subnet that goes through a next hop node,
        /// returning `Err` if no route was found to be deleted.
        fn del_next_hop_route(
            &mut self,
            subnet: Subnet<I::Addr>,
            next_hop: SpecifiedAddr<I::Addr>,
        ) -> Result<(), NotFoundError> {
            debug!("deleting next hop route: {} -> {}", subnet, next_hop);
            self.del_entry(Entry { subnet, dest: EntryDest::Remote { next_hop } })
        }

        /// Delete the route to a subnet that is considered on-link for a device,
        /// returning `Err` if no route was found to be deleted.
        fn del_device_route(
            &mut self,
            subnet: Subnet<I::Addr>,
            device: D,
        ) -> Result<(), NotFoundError> {
            debug!("deleting device route: {} -> {:?}", subnet, device);
            self.del_entry(Entry { subnet, dest: EntryDest::Local { device } })
        }

        /// Delete a route (`entry`) from this `ForwardingTable`, returning `Err` if
        /// the route did not already exist.
        fn del_entry(&mut self, entry: Entry<I::Addr, D>) -> Result<(), NotFoundError> {
            let Self { table } = self;
            let old_len = table.len();
            table.retain(|e| *e != entry);
            let new_len = table.len();

            if old_len == new_len {
                // If a path to `subnet` was not in our installed table, then it
                // definitely won't be in our active routes cache.
                return Err(NotFoundError);
            }

            // Must have deleted exactly 1 route if we reach this point.
            assert_eq!(old_len - new_len, 1);

            Ok(())
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

    #[ip_test]
    fn test_add_del_lookup_simple_ip<I: Ip + TestIpExt>() {
        let mut table = ForwardingTable::<I, DeviceId>::default();

        let config = I::DUMMY_CONFIG;
        let subnet = config.subnet;
        let device = DeviceId::new_ethernet(0);
        let next_hop = I::next_hop_addr();
        let next_hop_specific_subnet = Subnet::new(next_hop.get(), I::Addr::BYTES * 8).unwrap();

        // Should add the route successfully.
        table.add_device_route(subnet, device).unwrap();
        assert_eq!(table.iter_table().count(), 1);
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == subnet) && (x.dest == EntryDest::Local { device })));

        // Attempting to add the route again should fail.
        assert_eq!(table.add_device_route(subnet, device).unwrap_err(), ExistsError);
        assert_eq!(table.iter_table().count(), 1);

        // Add the route but as a next hop route.
        table.add_route(subnet, next_hop).unwrap();
        assert_eq!(table.iter_table().count(), 2);
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == subnet) && (x.dest == EntryDest::Local { device })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == subnet) && (x.dest == EntryDest::Remote { next_hop })));

        // Attempting to add the route again should fail.
        assert_eq!(table.add_route(subnet, next_hop).unwrap_err(), ExistsError);
        assert_eq!(table.iter_table().count(), 2);

        // Delete all routes to subnet.
        assert_eq!(
            table.del_route(subnet).unwrap().into_iter().collect::<HashSet<_>>(),
            HashSet::from([
                Entry { subnet, dest: EntryDest::Local { device } },
                Entry { subnet, dest: EntryDest::Remote { next_hop } }
            ])
        );
        assert_empty(table.iter_table());

        // Add the next hop route.
        table.add_route(subnet, next_hop).unwrap();
        assert_eq!(table.iter_table().count(), 1);
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == subnet) && (x.dest == EntryDest::Remote { next_hop })));

        // Attempting to add the next hop route again should fail.
        assert_eq!(table.add_route(subnet, next_hop).unwrap_err(), ExistsError);
        assert_eq!(table.iter_table().count(), 1);

        // Add a device route from the `next_hop` to some device.
        table.add_device_route(next_hop_specific_subnet, device).unwrap();
        assert_eq!(table.iter_table().count(), 2);
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == next_hop_specific_subnet)
                && (x.dest == EntryDest::Local { device })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == subnet) && (x.dest == EntryDest::Remote { next_hop })));

        // Do lookup for our next hop (should be the device).
        assert_eq!(table.lookup(None, next_hop).unwrap(), Destination { next_hop, device });

        // Do lookup for some address within `subnet`.
        assert_eq!(table.lookup(None, config.local_ip).unwrap(), Destination { next_hop, device });
        assert_eq!(table.lookup(None, config.remote_ip).unwrap(), Destination { next_hop, device });

        // Delete the device route.
        assert_eq!(
            table.del_route(next_hop_specific_subnet).unwrap(),
            &[Entry { subnet: next_hop_specific_subnet, dest: EntryDest::Local { device } }][..]
        );

        // Do lookup for our next hop (should get None since we have no route to
        // a local device).
        assert_eq!(table.lookup(None, next_hop), None);

        // Do lookup for some address within `subnet` (should get None as well).
        assert_eq!(table.lookup(None, config.local_ip), None);
        assert_eq!(table.lookup(None, config.remote_ip), None);
    }

    #[ip_test]
    fn test_max_depth_for_forwarding_table_ip<I: Ip + TestIpExt>() {
        let mut table = ForwardingTable::<I, DeviceId>::default();
        let device0 = DeviceId::new_ethernet(0);
        let device1 = DeviceId::new_ethernet(1);
        let (_, sub1) = I::next_hop_addr_sub(1, 24);
        let (addr2, sub2) = I::next_hop_addr_sub(2, 24);
        let (addr3, sub3) = I::next_hop_addr_sub(3, 24);
        let (addr4, sub4) = I::next_hop_addr_sub(4, 24);
        let (addr5, sub5) = I::next_hop_addr_sub(5, 24);

        // Add the following routes:
        //  sub1 -> addr2
        //  sub2 -> addr3
        //  sub3 -> addr4
        //  sub4 -> addr5
        //  sub3 -> device0
        //  sub5 -> device1
        //
        // Our expected forwarding table should look like:
        //  sub1 -> addr3 w/ device0
        //  sub2 -> addr3 w/ device0
        //  sub3 -> device0
        //  sub4 -> addr5 w/ device1
        //  sub5 -> device1

        table.add_route(sub1, addr2).unwrap();
        table.add_route(sub2, addr3).unwrap();
        table.add_route(sub3, addr4).unwrap();
        table.add_route(sub4, addr5).unwrap();
        table.add_device_route(sub3, device0).unwrap();
        table.add_device_route(sub5, device1).unwrap();
        table.print_table();
        assert_eq!(table.iter_table().count(), 6);
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub2) && (x.dest == EntryDest::Remote { next_hop: addr3 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Remote { next_hop: addr4 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub4) && (x.dest == EntryDest::Remote { next_hop: addr5 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Local { device: device0 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub5) && (x.dest == EntryDest::Local { device: device1 })));

        // Delete the route:
        //  sub3 -> device0
        //
        // Our expected forwarding table should look like:
        //  sub1 -> addr5 w/ device1
        //  sub2 -> addr5 w/ device1
        //  sub3 -> addr5 w/ device1
        //  sub4 -> addr5 w/ device1
        //  sub5 -> device1
        table.del_device_route(sub3, device0).unwrap();
        table.print_table();
        assert_eq!(table.iter_table().count(), 5);
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub2) && (x.dest == EntryDest::Remote { next_hop: addr3 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Remote { next_hop: addr4 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub4) && (x.dest == EntryDest::Remote { next_hop: addr5 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub5) && (x.dest == EntryDest::Local { device: device1 })));

        // Delete the route:
        //  sub3 -> addr4
        //
        // Our expected forwarding table should look like:
        //  sub4 -> addr5 w/ device1
        //  sub5 -> device1
        table.del_next_hop_route(sub3, addr4).unwrap();
        table.print_table();
        assert_eq!(table.iter_table().count(), 4);
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub2) && (x.dest == EntryDest::Remote { next_hop: addr3 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub4) && (x.dest == EntryDest::Remote { next_hop: addr5 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub5) && (x.dest == EntryDest::Local { device: device1 })));

        // Deleting routes that don't exist should fail
        assert_eq!(table.del_device_route(sub1, device0).unwrap_err(), NotFoundError);
        assert_eq!(table.del_next_hop_route(sub1, addr5).unwrap_err(), NotFoundError);
    }

    #[ip_test]
    fn test_use_most_specific_route_ip<I: Ip + TestIpExt>() {
        let mut table = ForwardingTable::<I, DeviceId>::default();
        let device0 = DeviceId::new_ethernet(0);
        let device1 = DeviceId::new_ethernet(1);
        let (addr7, sub7_s24) = I::next_hop_addr_sub(7, 24);
        let (addr8, sub8_s27) = I::next_hop_addr_sub(8, 27);
        let (addr10, sub10_s25) = I::next_hop_addr_sub(10, 25);
        let (addr12, sub12_s26) = I::next_hop_addr_sub(12, 26);
        let (addr14, sub14_s25) = I::next_hop_addr_sub(14, 25);
        let (addr15, _) = I::next_hop_addr_sub(15, 24);

        // In the following comments, we will use a modified form of prefix
        // notation. Normally to identify the prefix of an address, we do
        // ADDRESS/PREFIX (e.g. fe80::e80c:830f:1cc3:2336/64 for IPv6;
        // 100.96.232.33/24 for IPv4). Here, we will do ADDRESS/-SUFFIX to
        // represent the number of bits of the host portion of the address
        // instead of the network. The following is the relationship between
        // SUFFIX and PREFIX: PREFIX = ADDRESS_BITS - SUFFIX. So for the
        // examples given earlier:
        //  fe80::e80c:830f:1cc3:2336/64 <-> fe80::e80c:830f:1cc3:2336/-64
        //  100.96.232.33/24 <-> 100.96.232.33/-8
        //
        // We do this because this method is generic for IPv4 and IPv6 which
        // have different address lengths. To keep the comments consistent (at
        // the cost of some readability) we use this custom notation for the
        // comments below.
        //
        // Subnetting:
        //  sub10/-25, sub12/-26, sub14/-25 and sub15/-24 are subnets of sub8/-27.
        //  sub14/-25 and sub15/-24 are subnets of sub12/-26.
        //  sub15/-24 is a subnet of sub14/-25.

        // Add the following routes:
        //  sub8/-27 -> device0
        //
        // Our expected forwarding table should look like:
        //  sub8/-27 -> device0

        table.add_device_route(sub8_s27, device0).unwrap();
        table.print_table();
        assert_eq!(table.iter_table().count(), 1);
        assert_eq!(table.lookup(None, addr7), None);
        assert_eq!(
            table.lookup(None, addr8).unwrap(),
            Destination { next_hop: addr8, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr10).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr12).unwrap(),
            Destination { next_hop: addr12, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr14).unwrap(),
            Destination { next_hop: addr14, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr15).unwrap(),
            Destination { next_hop: addr15, device: device0 }
        );

        // Add the following routes:
        //  sub12/-26 -> device1
        //
        // Our expected forwarding table should look like:
        //  sub8/-27 -> device0
        //  sub12/-26 -> device1

        table.add_device_route(sub12_s26, device1).unwrap();
        table.print_table();
        assert_eq!(table.iter_table().count(), 2);
        assert_eq!(table.lookup(None, addr7), None);
        assert_eq!(
            table.lookup(None, addr8).unwrap(),
            Destination { next_hop: addr8, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr10).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr12).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(None, addr14).unwrap(),
            Destination { next_hop: addr14, device: device1 }
        );
        assert_eq!(
            table.lookup(None, addr15).unwrap(),
            Destination { next_hop: addr15, device: device1 }
        );

        // Add the following routes:
        //  sub14/-25 -> addr10
        //
        // Our expected forwarding table should look like:
        //  sub8/-27 -> device0
        //  sub12/-26 -> device1
        //  sub14/-25 -> addr10 w/ device0

        table.add_route(sub14_s25, addr10).unwrap();
        table.print_table();
        assert_eq!(table.iter_table().count(), 3);
        assert_eq!(
            table.lookup(None, addr8).unwrap(),
            Destination { next_hop: addr8, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr10).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr12).unwrap(),
            Destination { next_hop: addr12, device: device1 },
        );
        assert_eq!(
            table.lookup(None, addr14).unwrap(),
            Destination { next_hop: addr10, device: device0 },
            "addr = {}",
            addr14
        );
        assert_eq!(
            table.lookup(None, addr15).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );

        // This next two tests are important.
        //
        // Here, we add a route from sub10/-25 -> addr7. The routing table as no
        // route from addr7 so normally we would not have any route to the
        // subnet sub10/-25. However, we have a route for a less specific subnet
        // (sub8/-27) which IS routable so we use that instead.
        //
        // When we do eventually make sub7/-24 routable, sub10/-25 will be
        // routed through addr7.

        // Add the following routes:
        //  sub10/-25 -> addr7
        //
        // Our expected forwarding table should look like:
        //  sub8/-27 -> device0
        //  sub12/-26 -> device1
        //  sub14/-25 -> addr10 w/ device0

        table.add_route(sub10_s25, addr7).unwrap();
        table.print_table();
        assert_eq!(table.iter_table().count(), 4);
        assert_eq!(table.lookup(None, addr7), None);
        assert_eq!(
            table.lookup(None, addr8).unwrap(),
            Destination { next_hop: addr8, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr10).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr12).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(None, addr14).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr15).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );

        // Add the following routes:
        //  sub7/-24 -> addr12
        //
        // Our expected forwarding table should look like:
        //  sub8/-27 -> device0
        //  sub12/-26 -> device1
        //  sub14/-25 -> addr12 w/ deviec1
        //  sub7/-24 -> addr12 w/ device1
        //  sub10/-25 -> addr12 w/ device1

        table.add_route(sub7_s24, addr12).unwrap();
        table.print_table();
        assert_eq!(table.iter_table().count(), 5);
        assert_eq!(
            table.lookup(None, addr7).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(None, addr8).unwrap(),
            Destination { next_hop: addr8, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr10).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(None, addr12).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(None, addr14).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(None, addr15).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );

        // Add the following routes:
        //  sub14/-25 -> device0
        //
        // Our expected forwarding table should look like:
        //  sub8/-27 -> device0
        //  sub12/-26 -> device1
        //  sub7/-24 -> addr12 w/ device1
        //  sub10/-25 -> addr12 w/ device1
        //  sub14/-25 -> device0

        table.add_device_route(sub14_s25, device0).unwrap();
        table.print_table();
        assert_eq!(table.iter_table().count(), 6);
        assert_eq!(
            table.lookup(None, addr7).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(None, addr8).unwrap(),
            Destination { next_hop: addr8, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr10).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(None, addr12).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(None, addr14).unwrap(),
            Destination { next_hop: addr14, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr15).unwrap(),
            Destination { next_hop: addr15, device: device0 }
        );

        // Check the installed table just in case.
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub8_s27) && (x.dest == EntryDest::Local { device: device0 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub12_s26) && (x.dest == EntryDest::Local { device: device1 })));
        assert!(
            table
                .iter_table()
                .any(|x| (x.subnet == sub14_s25)
                    && (x.dest == EntryDest::Remote { next_hop: addr10 }))
        );
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub10_s25) && (x.dest == EntryDest::Remote { next_hop: addr7 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub7_s24) && (x.dest == EntryDest::Remote { next_hop: addr12 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub14_s25) && (x.dest == EntryDest::Local { device: device0 })));
    }

    #[ip_test]
    fn test_cycle_ip<I: Ip + TestIpExt>() {
        let mut table = ForwardingTable::<I, DeviceId>::default();
        let device0 = DeviceId::new_ethernet(0);
        let (addr1, sub1) = I::next_hop_addr_sub(1, 24);
        let (addr2, sub2) = I::next_hop_addr_sub(2, 24);
        let (addr3, sub3) = I::next_hop_addr_sub(3, 24);
        let (addr4, sub4) = I::next_hop_addr_sub(4, 24);
        let (addr5, _) = I::next_hop_addr_sub(5, 24);

        // Add the following routes:
        //  sub1 -> addr2
        //  sub2 -> addr2 (cycle)
        //  sub3 -> addr4
        //  sub4 -> addr5
        //  sub3 -> device0
        //
        // Our expected forwarding table should look like:
        //  sub3 -> device0

        table.add_route(sub1, addr2).unwrap();
        table.add_route(sub2, addr2).unwrap();
        table.add_route(sub3, addr4).unwrap();
        table.add_route(sub4, addr5).unwrap();
        table.add_device_route(sub3, device0).unwrap();
        table.print_table();
        assert_eq!(table.iter_table().count(), 5);
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub2) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Remote { next_hop: addr4 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub4) && (x.dest == EntryDest::Remote { next_hop: addr5 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Local { device: device0 })));
        assert_eq!(table.lookup(None, addr1), None);
        assert_eq!(table.lookup(None, addr2), None);
        assert_eq!(
            table.lookup(None, addr3).unwrap(),
            Destination { next_hop: addr3, device: device0 }
        );
        assert_eq!(table.lookup(None, addr4), None);
        assert_eq!(table.lookup(None, addr5), None);

        // Keep the route with the cycle, but add another route that doesn't
        // have a cycle for sub2.
        //
        // Add the following routes:
        //  sub2 -> addr3
        //
        // Our expected forwarding table should look like:
        //  sub3 -> device0
        //  sub1 -> addr3 w/ device0
        //  sub2 -> addr3 w/ device0

        table.add_route(sub2, addr3).unwrap();
        table.print_table();
        assert_eq!(table.iter_table().count(), 6);
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub2) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Remote { next_hop: addr4 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub4) && (x.dest == EntryDest::Remote { next_hop: addr5 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Local { device: device0 })));
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub2) && (x.dest == EntryDest::Remote { next_hop: addr3 })));
        assert_eq!(
            table.lookup(None, addr1).unwrap(),
            Destination { next_hop: addr3, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr2).unwrap(),
            Destination { next_hop: addr3, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr3).unwrap(),
            Destination { next_hop: addr3, device: device0 }
        );
        assert_eq!(table.lookup(None, addr4), None);
        assert_eq!(table.lookup(None, addr5), None);
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

        table.add_device_route(sub1, device0).unwrap();
        table.print_table();
        assert_eq!(table.iter_table().count(), 1);
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Local { device: device0 })));
        assert_eq!(
            table.lookup(None, addr1).unwrap(),
            Destination { next_hop: addr1, device: device0 }
        );
        assert_eq!(table.lookup(None, addr2), None);

        // Add a default route.
        //
        // Our expected forwarding table should look like:
        //  sub1 -> device0
        //  default -> addr1 w/ device0

        let default_sub = Subnet::new(I::UNSPECIFIED_ADDRESS, 0).unwrap();
        table.add_route(default_sub, addr1).unwrap();
        assert_eq!(table.iter_table().count(), 2);
        assert!(table
            .iter_table()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Local { device: device0 })));
        assert!(table.iter_table().any(
            |x| (x.subnet == default_sub) && (x.dest == EntryDest::Remote { next_hop: addr1 })
        ));
        assert_eq!(
            table.lookup(None, addr1).unwrap(),
            Destination { next_hop: addr1, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr2).unwrap(),
            Destination { next_hop: addr1, device: device0 }
        );
        assert_eq!(
            table.lookup(None, addr3).unwrap(),
            Destination { next_hop: addr1, device: device0 }
        );
    }

    #[ip_test]
    fn test_device_filter<I: Ip + TestIpExt>() {
        const MORE_SPECIFIC_SUB_DEVICE: u8 = 1;
        const LESS_SPECIFIC_SUB_DEVICE: u8 = 2;

        let mut table = ForwardingTable::<I, u8>::default();
        let (next_hop, more_specific_sub) = I::next_hop_addr_sub(1, 1);
        let less_specific_sub = {
            let (addr, sub) = I::next_hop_addr_sub(1, 2);
            assert_eq!(next_hop, addr);
            sub
        };

        table.add_device_route(less_specific_sub, LESS_SPECIFIC_SUB_DEVICE).unwrap();
        assert_eq!(
            table.lookup(None, next_hop),
            Some(Destination { next_hop, device: LESS_SPECIFIC_SUB_DEVICE }),
            "matches route"
        );
        assert_eq!(
            table.lookup(Some(LESS_SPECIFIC_SUB_DEVICE), next_hop),
            Some(Destination { next_hop, device: LESS_SPECIFIC_SUB_DEVICE }),
            "route matches specified device"
        );
        assert_eq!(
            table.lookup(Some(MORE_SPECIFIC_SUB_DEVICE), next_hop),
            None,
            "no route with the specified device"
        );

        table.add_device_route(more_specific_sub, MORE_SPECIFIC_SUB_DEVICE).unwrap();
        assert_eq!(
            table.lookup(None, next_hop).unwrap(),
            Destination { next_hop, device: MORE_SPECIFIC_SUB_DEVICE },
            "matches most specific route"
        );
        assert_eq!(
            table.lookup(Some(LESS_SPECIFIC_SUB_DEVICE), next_hop),
            Some(Destination { next_hop, device: LESS_SPECIFIC_SUB_DEVICE }),
            "matches less specific route with the specified device"
        );
        assert_eq!(
            table.lookup(Some(MORE_SPECIFIC_SUB_DEVICE), next_hop).unwrap(),
            Destination { next_hop, device: MORE_SPECIFIC_SUB_DEVICE },
            "matches the most specific route with the specified device"
        );
    }

    #[ip_test]
    fn test_multiple_routes_to_subnet_through_different_devices<I: Ip + TestIpExt>() {
        const DEVICE1: u8 = 1;
        const DEVICE2: u8 = 2;

        let mut table = ForwardingTable::<I, u8>::default();
        let (next_hop, sub) = I::next_hop_addr_sub(1, 1);

        table.add_device_route(sub, DEVICE1).unwrap();
        table.add_device_route(sub, DEVICE2).unwrap();
        let lookup = table.lookup(None, next_hop);
        assert!(
            [
                Some(Destination { next_hop, device: DEVICE1 }),
                Some(Destination { next_hop, device: DEVICE2 })
            ]
            .contains(&lookup),
            "lookup = {:?}",
            lookup
        );
        assert_eq!(
            table.lookup(Some(DEVICE1), next_hop),
            Some(Destination { next_hop, device: DEVICE1 }),
        );
        assert_eq!(
            table.lookup(Some(DEVICE2), next_hop),
            Some(Destination { next_hop, device: DEVICE2 }),
        );
    }
}
