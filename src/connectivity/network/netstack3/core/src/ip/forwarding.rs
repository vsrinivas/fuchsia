// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use alloc::collections::HashSet;
use core::fmt::Debug;

use net_types::ip::{Ip, IpAddress, Subnet};
use net_types::SpecifiedAddr;

use crate::ip::*;

// TODO(joshlf):
// - How do we detect circular routes? Do we attempt to detect at rule
//   installation time? At runtime? Using what algorithm?

// NOTE on loopback addresses: Loopback addresses should be handled before
// reaching the forwarding table. For that reason, we do not prevent a rule
// whose subnet is a subset of the loopback subnet from being installed; they
// will never get triggered anyway, so implementing the logic of detecting these
// rules is a needless complexity.

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

/// An active forwarding entry.
///
/// See [`ForwardingTable::active`].
#[derive(Debug, PartialEq, Eq)]
struct ActiveEntry<A: IpAddress, D> {
    subnet: Subnet<A>,
    dest: ActiveEntryDest<A, D>,
}

/// An active forwarding entry's destination.
///
/// See [`ActiveEntry`] and [`ForwardingTable::active`].
#[derive(Debug, PartialEq, Eq)]
enum ActiveEntryDest<A: IpAddress, D> {
    Local { device: D },
    Remote { dest: Destination<A, D> },
}

/// An IP forwarding table.
///
/// `ForwardingTable` maps destination subnets to the nearest IP hosts (on the
/// local network) able to route IP packets to those subnets.
// TODO(ghanan): Use metrics to determine active route?
pub(crate) struct ForwardingTable<I: Ip, D> {
    /// A cache of the active routes to use when forwarding a packet.
    ///
    /// `active` MUST NOT have redundant (even if unique) paths to the same destination to ensure
    /// that all packets to the same destination use the same path (assuming no changes happen to
    /// the forwarding table between packets).
    // TODO(ghanan): Loosen this restriction and support load balancing?
    active: Vec<ActiveEntry<I::Addr, D>>,

    /// All the routes available to forward a packet.
    ///
    /// `installed` may have redundant, but unique, paths to the same destination. Only the best
    /// routes should be put into `active`.
    installed: Vec<Entry<I::Addr, D>>,
}

impl<I: Ip, D> Default for ForwardingTable<I, D> {
    fn default() -> ForwardingTable<I, D> {
        ForwardingTable { active: Vec::new(), installed: Vec::new() }
    }
}

impl<I: Ip, D: Clone + Debug + PartialEq> ForwardingTable<I, D> {
    /// Do we already have the route installed in our forwarding table?
    ///
    /// `contains_entry` returns `true` if this `ForwardingTable` is already aware of the exact
    /// route by `entry`.
    fn contains_entry(&self, entry: &Entry<I::Addr, D>) -> bool {
        self.installed.iter().any(|e| e == entry)
    }

    /// Adds `entry` to the forwarding table if it does not already exist.
    fn add_entry(&mut self, entry: Entry<I::Addr, D>) -> Result<(), ExistsError> {
        if self.contains_entry(&entry) {
            // If we already have this exact route, don't add it again.
            Err(ExistsError)
        } else {
            // Add the route to our installed table.
            //
            // TODO(ghanan): Check for cycles?
            self.installed.push(entry);

            // Regenerate our active routes.
            self.regen_active();

            Ok(())
        }
    }

    // TODO(joshlf): Should `next_hop` actually be restricted even further,
    // perhaps to unicast addresses?

    /// Add a route to a destination subnet that requires going through another node.
    pub(crate) fn add_route(
        &mut self,
        subnet: Subnet<I::Addr>,
        next_hop: SpecifiedAddr<I::Addr>,
    ) -> Result<(), ExistsError> {
        debug!("adding route: {} -> {}", subnet, next_hop);
        self.add_entry(Entry { subnet, dest: EntryDest::Remote { next_hop } })
    }

    /// Add a route to a destination subnet that lives on a link an interface is attached to.
    pub(crate) fn add_device_route(
        &mut self,
        subnet: Subnet<I::Addr>,
        device: D,
    ) -> Result<(), ExistsError> {
        debug!("adding device route: {} -> {:?}", subnet, device);
        self.add_entry(Entry { subnet, dest: EntryDest::Local { device } })
    }

    /// Delete all routes to a subnet, returning `Err` if no route was found to be deleted.
    ///
    /// Note, `del_route` will remove *all* routes to a `subnet`, including routes that consider
    /// `subnet` on-link for some device and routes that require packets destined to a node
    /// within `subnet` to be routed through some next-hop node.
    pub(crate) fn del_route(&mut self, subnet: Subnet<I::Addr>) -> Result<(), NotFoundError> {
        debug!("deleting route: {}", subnet);

        // Delete all routes to a subnet.
        let old_len = self.installed.len();
        self.installed.retain(|entry| entry.subnet != subnet);
        let new_len = self.installed.len();

        if old_len == new_len {
            // If a path to `subnet` was not in our installed table, then it definitely won't be in
            // our active routes cache.
            return Err(NotFoundError);
        }

        // Regenerate our cache of active routes.
        self.regen_active();

        Ok(())
    }

    /// Delete the route to a subnet that goes through a next hop node, returning `Err` if no
    /// route was found to be deleted.
    // TODO(rheacock): remove `allow(dead_code)` when this is used.
    #[allow(dead_code)]
    pub(crate) fn del_next_hop_route(
        &mut self,
        subnet: Subnet<I::Addr>,
        next_hop: SpecifiedAddr<I::Addr>,
    ) -> Result<(), NotFoundError> {
        debug!("deleting next hop route: {} -> {}", subnet, next_hop);
        self.del_entry(Entry { subnet, dest: EntryDest::Remote { next_hop } })
    }

    /// Delete the route to a subnet that is considerd on-link for a device, returning `Err` if no
    /// route was found to be deleted.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn del_device_route(
        &mut self,
        subnet: Subnet<I::Addr>,
        device: D,
    ) -> Result<(), NotFoundError> {
        debug!("deleting device route: {} -> {:?}", subnet, device);
        self.del_entry(Entry { subnet, dest: EntryDest::Local { device } })
    }

    /// Delete a route (`entry`) from this `ForwardingTable`, returning `Err` if the route did not
    /// already exist.
    fn del_entry(&mut self, entry: Entry<I::Addr, D>) -> Result<(), NotFoundError> {
        let old_len = self.installed.len();
        self.installed.retain(|e| *e != entry);
        let new_len = self.installed.len();

        if old_len == new_len {
            // If a path to `subnet` was not in our installed table, then it definitely won't be in
            // our active routes cache.
            return Err(NotFoundError);
        }

        // Must have deleted exactly 1 route if we reach this point.
        assert_eq!(old_len - new_len, 1);

        // Regenerate our cache of active routes.
        self.regen_active();

        Ok(())
    }

    /// Look up an address in the table.
    ///
    /// Look up an IP address in the table, returning a next hop IP address and
    /// a device to send over. If `address` is link-local, then the returned
    /// next hop will be `address`. Otherwise, it will be the link-local address
    /// of an IP router capable of delivering packets to `address`.
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
    ///
    /// # Panics
    ///
    /// `lookup` asserts that `address` is not in the loopback interface.
    /// Traffic destined for loopback addresses from local applications should
    /// be properly routed without consulting the forwarding table, and traffic
    /// from the network with a loopback destination address is invalid and
    /// should be dropped before consulting the forwarding table.
    pub(crate) fn lookup(
        &self,
        address: SpecifiedAddr<I::Addr>,
    ) -> Option<Destination<I::Addr, D>> {
        assert!(
            !I::LOOPBACK_SUBNET.contains(&address),
            "loopback addresses should be handled before consulting the forwarding table"
        );

        let best_match = self
            .active
            .iter()
            .filter(|e| e.subnet.contains(&address))
            .max_by_key(|e| e.subnet.prefix())
            .map(|e| &e.dest);

        trace!("lookup({}) -> {:?}", address, best_match);

        match best_match {
            Some(ActiveEntryDest::Local { device }) => {
                Some(Destination { next_hop: address, device: device.clone() })
            }
            Some(ActiveEntryDest::Remote { dest }) => Some(dest.clone()),
            None => None,
        }
    }

    /// Get an iterator over all of the forwarding entries ([`Entry`]) this `ForwardingTable`
    /// knows about.
    pub(crate) fn iter_installed(&self) -> core::slice::Iter<Entry<I::Addr, D>> {
        self.installed.iter()
    }

    /// Generate our cache of the active routes to use.
    ///
    /// `regen_active` will regenerate the active routes used by this `ForwardingTable` when looking
    /// up the next hop for a packet. This method will ensure that the cache will not have any
    /// redundant paths to any destination. For any destination, preference will be given to paths
    /// that require the least amount of hops through routers, as known by the installed table when
    /// this method was called.
    // TODO(ghanan): Come up with a more performant algorithm.
    fn regen_active(&mut self) {
        let mut subnets = HashSet::new();
        let mut new_active = Vec::new();

        // Insert all the subnets we have paths for.
        for e in self.installed.iter() {
            // We already tried to find a path to `subnet`, skip.
            if subnets.contains(&e.subnet) {
                continue;
            }

            // Mark `subnet` so we know we already tried to find a route to it.
            subnets.insert(e.subnet);

            // If a route to `subnet` exists, store it in our new active routes cache.
            //
            // TODO(ghanan): When regenerating the active table, use the new active table as we
            //               generate it to help with lookups?
            if let Some(dest) = self.regen_active_helper(&e.subnet) {
                new_active.push(ActiveEntry { subnet: e.subnet, dest });
            }
        }

        self.active = new_active;
    }

    /// Find the final destination a packet destined to an address in `subnet` should be routed to
    /// by inspecting the installed table.
    ///
    /// Preference will be given to an on-link destination.
    fn regen_active_helper(&self, subnet: &Subnet<I::Addr>) -> Option<ActiveEntryDest<I::Addr, D>> {
        // The best route requiring a next-hop node.
        let mut best_remote = None;

        for e in self.installed.iter().filter(|e| e.subnet == *subnet) {
            match &e.dest {
                EntryDest::Local { device } => {
                    // Return routes that consider `subnet` as on-link immediately.
                    return Some(ActiveEntryDest::Local { device: device.clone() });
                }
                EntryDest::Remote { next_hop } => {
                    // If we already have a route going through a next-hop node, skip.
                    if best_remote.is_some() {
                        continue;
                    }

                    // If the subnet requires a next hop, attempt to resolve the route to the next
                    // hop. If no route exists, ignore this potential match as we have no path to
                    // `next_hop` with this route. If a route exists, store it to return if no
                    // route exists that considers `subnet` an on-link subnet.
                    if let Some(dest) = self.installed_lookup(*next_hop) {
                        best_remote = Some(ActiveEntryDest::Remote { dest });
                    }
                }
            }
        }

        best_remote
    }

    /// Find the destination a packet destined to `address` should be routed to by inspecting the
    /// installed table.
    fn installed_lookup(&self, address: SpecifiedAddr<I::Addr>) -> Option<Destination<I::Addr, D>> {
        use alloc::vec;

        let mut observed = vec![false; self.installed.len()];
        self.installed_lookup_helper(address, &mut observed)
    }

    /// Find the destination a packet destined to `address` should be routed to by inspecting the
    /// installed table.
    ///
    /// `observed` will be marked with each entry we have already observed to prevent loops.
    fn installed_lookup_helper(
        &self,
        address: SpecifiedAddr<I::Addr>,
        observed: &mut Vec<bool>,
    ) -> Option<Destination<I::Addr, D>> {
        // Get all potential routes we could take to reach `address`.
        let q = self.installed.iter().enumerate().filter(|(_, e)| e.subnet.contains(&address));

        // The best route to reach `address` so far.
        //
        // Tuple of (ADDRESS_PREFIX, `Destination`).
        let mut best_so_far: Option<(u8, Destination<I::Addr, D>)> = None;

        for (i, e) in q {
            // Check if we already observed this entry.
            if observed[i] {
                // If we already observed this entry, then that means we are hitting it again,
                // indicating the following:
                //  1) There is a loop.
                //  2) The last time we hit this entry, we did not find a valid destination.
                // Given what we know, we skip checking it again.
                continue;
            }

            // Mark the entry as now observed.
            observed[i] = true;

            match &e.dest {
                EntryDest::Local { device } => {
                    // If we have a best route so far and its subnet prefix is greater than the one
                    // we are looking at right now, skip. Otherwise, if the the subnet prefix is
                    // less than the one we are looking at right now, or the prefixes are equal but
                    // the existing best destination is a remote, update to the this local.
                    if let Some(best_so_far) = &mut best_so_far {
                        if best_so_far.0 > e.subnet.prefix() {
                            continue;
                        } else if best_so_far.0 < e.subnet.prefix()
                            || best_so_far.1.next_hop != address
                        {
                            // If the prefixes are equal, we know this is a remote because for local
                            // destinations, the next hop MUST match `address`.
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
                    // If we have a best route so far and its subnet prefix is greater than or equal
                    // to the one we are looking at right now, skip.
                    if let Some(best_so_far) = best_so_far.clone() {
                        if best_so_far.0 >= e.subnet.prefix() {
                            continue;
                        }
                    }

                    // If the subnet requires a next hop, attempt to resolve the route to the next
                    // hop. If no route exists, ignore this potential match as we have no path to
                    // `address` with this route. If a route exists, keep it as the best route so
                    // far.
                    if let Some(dest) = self.installed_lookup_helper(*next_hop, observed) {
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
    use specialize_ip_macro::ip_test;

    use super::*;

    use crate::device::DeviceId;
    use crate::testutil::TestIpExt;

    impl<I: Ip, D: Clone + Debug + PartialEq> ForwardingTable<I, D> {
        /// Print the active and installed forwarding table.
        pub(crate) fn print(&self) {
            self.print_installed();
            self.print_active();
        }

        /// Print the active routes forwarding table.
        fn print_active(&self) {
            trace!("Forwarding table:");

            if self.active.is_empty() {
                trace!("    No Routes");
                return;
            }

            for e in self.iter_active() {
                trace!("    {} -> {:?} ", e.subnet, e.dest);
            }
        }

        /// Print the installed table.
        fn print_installed(&self) {
            trace!("Installed Routing table:");

            if self.installed.is_empty() {
                trace!("    No Routes");
                return;
            }

            for e in self.iter_installed() {
                trace!("    {} -> {:?} ", e.subnet, e.dest);
            }
        }

        /// Get an iterator over the active forwarding entries ([`Entry`]) this `ForwardingTable`
        /// knows about.
        fn iter_active(&self) -> std::slice::Iter<ActiveEntry<I::Addr, D>> {
            self.active.iter()
        }
    }

    #[specialize_ip]
    fn sub<I: Ip>(v: u8, neg_prefix: u8) -> Subnet<I::Addr> {
        #[ipv4]
        return Subnet::new(Ipv4Addr::new([v, 0, 0, 0]), 32 - neg_prefix).unwrap();

        #[ipv6]
        return Subnet::new(
            Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, v, 0, 0, 0]),
            128 - neg_prefix,
        )
        .unwrap();
    }

    #[specialize_ip]
    fn next_hop_addr_sub<I: Ip>(
        v: u8,
        neg_prefix: u8,
    ) -> (SpecifiedAddr<I::Addr>, Subnet<I::Addr>) {
        #[ipv4]
        return (
            SpecifiedAddr::new(Ipv4Addr::new([v, 0, 0, 1])).unwrap(),
            sub::<Ipv4>(v, neg_prefix),
        );

        #[ipv6]
        return (
            SpecifiedAddr::new(Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, v, 0, 0, 1]))
                .unwrap(),
            sub::<Ipv6>(v, neg_prefix),
        );
    }

    #[specialize_ip]
    fn next_hop_addr<I: Ip>() -> SpecifiedAddr<I::Addr> {
        #[ipv4]
        return SpecifiedAddr::new(Ipv4Addr::new([10, 0, 0, 1])).unwrap();

        #[ipv6]
        return SpecifiedAddr::new(Ipv6Addr::new([
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 1,
        ]))
        .unwrap();
    }

    #[ip_test]
    fn test_add_del_lookup_simple_ip<I: Ip + TestIpExt>() {
        let mut table = ForwardingTable::<I, DeviceId>::default();

        let config = I::DUMMY_CONFIG;
        let subnet = config.subnet;
        let device = DeviceId::new_ethernet(0);
        let next_hop = next_hop_addr::<I>();
        let next_hop_specific_subnet = Subnet::new(next_hop.get(), I::Addr::BYTES * 8).unwrap();

        // Should add the route successfully.
        table.add_device_route(subnet, device).unwrap();
        assert_eq!(table.iter_active().count(), 1);
        assert_eq!(table.iter_installed().count(), 1);
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == subnet) && (x.dest == ActiveEntryDest::Local { device })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == subnet) && (x.dest == EntryDest::Local { device })));

        // Attempting to add the route again should fail.
        assert_eq!(table.add_device_route(subnet, device).unwrap_err(), ExistsError);
        assert_eq!(table.iter_active().count(), 1);
        assert_eq!(table.iter_installed().count(), 1);

        // Add the route but as a next hop route.
        table.add_route(subnet, next_hop).unwrap();
        assert_eq!(table.iter_active().count(), 1);
        assert_eq!(table.iter_installed().count(), 2);
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == subnet) && (x.dest == ActiveEntryDest::Local { device })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == subnet) && (x.dest == EntryDest::Local { device })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == subnet) && (x.dest == EntryDest::Remote { next_hop })));

        // Attempting to add the route again should fail.
        assert_eq!(table.add_route(subnet, next_hop).unwrap_err(), ExistsError);
        assert_eq!(table.iter_active().count(), 1);
        assert_eq!(table.iter_installed().count(), 2);

        // Delete all routes to subnet.
        table.del_route(subnet).unwrap();
        assert_eq!(table.iter_active().count(), 0);
        assert_eq!(table.iter_installed().count(), 0);

        // Add the next hop route.
        table.add_route(subnet, next_hop).unwrap();
        assert_eq!(table.iter_active().count(), 0);
        assert_eq!(table.iter_installed().count(), 1);
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == subnet) && (x.dest == EntryDest::Remote { next_hop })));

        // Attempting to add the next hop route again should fail.
        assert_eq!(table.add_route(subnet, next_hop).unwrap_err(), ExistsError);
        assert_eq!(table.iter_active().count(), 0);
        assert_eq!(table.iter_installed().count(), 1);

        // Add a device route from the `next_hop` to some device.
        table.add_device_route(next_hop_specific_subnet, device).unwrap();
        assert_eq!(table.iter_active().count(), 2);
        assert_eq!(table.iter_installed().count(), 2);
        assert!(table.iter_active().any(|x| (x.subnet == subnet)
            && (x.dest == ActiveEntryDest::Remote { dest: Destination { next_hop, device } })));
        assert!(table.iter_active().any(|x| (x.subnet == next_hop_specific_subnet)
            && (x.dest == ActiveEntryDest::Local { device })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == next_hop_specific_subnet)
                && (x.dest == EntryDest::Local { device })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == subnet) && (x.dest == EntryDest::Remote { next_hop })));

        // Do lookup for our next hop (should be the device).
        assert_eq!(table.lookup(next_hop).unwrap(), Destination { next_hop, device });

        // Do lookup for some address within `subnet`.
        assert_eq!(table.lookup(config.local_ip).unwrap(), Destination { next_hop, device });
        assert_eq!(table.lookup(config.remote_ip).unwrap(), Destination { next_hop, device });

        // Delete the device route.
        table.del_route(next_hop_specific_subnet).unwrap();

        // Do lookup for our next hop (should get None since we have no route to a local device).
        assert!(table.lookup(next_hop).is_none());

        // Do lookup for some address within `subnet` (should get None as well).
        assert!(table.lookup(config.local_ip).is_none());
        assert!(table.lookup(config.remote_ip).is_none());
    }

    #[ip_test]
    fn test_max_depth_for_forwarding_table_ip<I: Ip>() {
        let mut table = ForwardingTable::<I, DeviceId>::default();
        let device0 = DeviceId::new_ethernet(0);
        let device1 = DeviceId::new_ethernet(1);
        let (_, sub1) = next_hop_addr_sub::<I>(1, 24);
        let (addr2, sub2) = next_hop_addr_sub::<I>(2, 24);
        let (addr3, sub3) = next_hop_addr_sub::<I>(3, 24);
        let (addr4, sub4) = next_hop_addr_sub::<I>(4, 24);
        let (addr5, sub5) = next_hop_addr_sub::<I>(5, 24);

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
        table.print();
        assert_eq!(table.iter_active().count(), 5);
        assert_eq!(table.iter_installed().count(), 6);
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub2) && (x.dest == EntryDest::Remote { next_hop: addr3 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Remote { next_hop: addr4 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub4) && (x.dest == EntryDest::Remote { next_hop: addr5 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Local { device: device0 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub5) && (x.dest == EntryDest::Local { device: device1 })));
        assert!(table.iter_active().any(|x| (x.subnet == sub1)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr3, device: device0 }
                })));
        assert!(table.iter_active().any(|x| (x.subnet == sub2)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr3, device: device0 }
                })));
        assert!(table.iter_active().any(|x| (x.subnet == sub4)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr5, device: device1 }
                })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub3) && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub5) && (x.dest == ActiveEntryDest::Local { device: device1 })));

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
        table.print();
        assert_eq!(table.iter_active().count(), 5);
        assert_eq!(table.iter_installed().count(), 5);
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub2) && (x.dest == EntryDest::Remote { next_hop: addr3 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Remote { next_hop: addr4 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub4) && (x.dest == EntryDest::Remote { next_hop: addr5 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub5) && (x.dest == EntryDest::Local { device: device1 })));
        assert!(table.iter_active().any(|x| (x.subnet == sub1)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr5, device: device1 }
                })));
        assert!(table.iter_active().any(|x| (x.subnet == sub2)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr5, device: device1 }
                })));
        assert!(table.iter_active().any(|x| (x.subnet == sub3)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr5, device: device1 }
                })));
        assert!(table.iter_active().any(|x| (x.subnet == sub4)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr5, device: device1 }
                })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub5) && (x.dest == ActiveEntryDest::Local { device: device1 })));

        // Delete the route:
        //  sub3 -> addr4
        //
        // Our expected forwarding table should look like:
        //  sub4 -> addr5 w/ device1
        //  sub5 -> device1
        table.del_next_hop_route(sub3, addr4).unwrap();
        table.print();
        assert_eq!(table.iter_active().count(), 2);
        assert_eq!(table.iter_installed().count(), 4);
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub2) && (x.dest == EntryDest::Remote { next_hop: addr3 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub4) && (x.dest == EntryDest::Remote { next_hop: addr5 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub5) && (x.dest == EntryDest::Local { device: device1 })));
        assert!(table.iter_active().any(|x| (x.subnet == sub4)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr5, device: device1 }
                })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub5) && (x.dest == ActiveEntryDest::Local { device: device1 })));

        // Deleting routes that don't exist should fail
        assert_eq!(table.del_device_route(sub1, device0).unwrap_err(), NotFoundError);
        assert_eq!(table.del_next_hop_route(sub1, addr5).unwrap_err(), NotFoundError);
    }

    #[ip_test]
    fn test_find_active_route_if_it_exists_ip<I: Ip>() {
        let mut table = ForwardingTable::<I, DeviceId>::default();
        let device0 = DeviceId::new_ethernet(0);
        let device1 = DeviceId::new_ethernet(1);
        let (addr1, sub1_s24) = next_hop_addr_sub::<I>(1, 24);
        let (addr2, sub2_s24) = next_hop_addr_sub::<I>(2, 24);
        let (addr3, _) = next_hop_addr_sub::<I>(3, 24);
        let (addr4, sub4_s24) = next_hop_addr_sub::<I>(4, 24);
        let (addr5, sub5_s24) = next_hop_addr_sub::<I>(5, 24);

        // In the following comments, we will used a modified form of prefix notation.
        // Normally to identify the prefix of an address, we do ADDRESS/PREFIX (e.g.
        // fe80::e80c:830f:1cc3:2336/64 for IPv6; 100.96.232.33/24 for IPv4).
        // Here, we will do ADDRESS/-SUFFIX to represent the number of bits of the
        // host portion of the address instead of the network. The following is the
        // relationship between SUFFIX and PREFIX: PREFIX = ADDRESS_BITS - SUFFIX.
        // So for the examples given earlier:
        //  fe80::e80c:830f:1cc3:2336/64 <-> fe80::e80c:830f:1cc3:2336/-64
        //  100.96.232.33/24 <-> 100.96.232.33/-8
        //
        // We do this because this method is generic for IPv4 and IPv6 which have different
        // address lengths. To keep the comments consistent (at the cost of some readability)
        // we use this custom notation for the comments below.
        //
        // Add the following routes:
        //  sub1/-24 -> addr2
        //  sub2/-24 -> addr4
        //  sub2/-24 -> addr3
        //  sub4/-24 -> addr5
        //  sub5/-24 -> device0
        //
        // Our expected forwarding table should look like:
        //  sub1/-24 -> addr5 w/ device0
        //  sub2/-24 -> addr5 w/ device0
        //  sub4/-24 -> addr5 w/ device0
        //  sub5/-24 -> device0

        table.add_route(sub1_s24, addr2).unwrap();
        table.add_route(sub2_s24, addr4).unwrap();
        table.add_route(sub2_s24, addr3).unwrap();
        table.add_route(sub4_s24, addr5).unwrap();
        table.add_device_route(sub5_s24, device0).unwrap();
        table.print();
        assert_eq!(table.iter_active().count(), 4);
        assert_eq!(table.iter_installed().count(), 5);
        assert!(table.iter_active().any(|x| (x.subnet == sub1_s24)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr5, device: device0 }
                })));
        assert!(table.iter_active().any(|x| (x.subnet == sub2_s24)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr5, device: device0 }
                })));
        assert!(table.iter_active().any(|x| (x.subnet == sub4_s24)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr5, device: device0 }
                })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub5_s24)
                && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert_eq!(table.lookup(addr1).unwrap(), Destination { next_hop: addr5, device: device0 });
        assert_eq!(table.lookup(addr2).unwrap(), Destination { next_hop: addr5, device: device0 });
        assert_eq!(table.lookup(addr5).unwrap(), Destination { next_hop: addr5, device: device0 });

        // Add the following routes:
        //  sub1/-24 -> device1
        //
        // Our expected forwarding table should look like:
        //  sub2/-24 -> addr5 w/ device0
        //  sub4/-24 -> addr5 w/ device0
        //  sub5/-24 -> device0
        //  sub1/-24 -> device1
        table.add_device_route(sub1_s24, device1).unwrap();
        table.print();
        assert_eq!(table.iter_active().count(), 4);
        assert_eq!(table.iter_installed().count(), 6);
        assert!(table.iter_active().any(|x| (x.subnet == sub2_s24)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr5, device: device0 }
                })));
        assert!(table.iter_active().any(|x| (x.subnet == sub4_s24)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr5, device: device0 }
                })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub5_s24)
                && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub1_s24)
                && (x.dest == ActiveEntryDest::Local { device: device1 })));
        assert_eq!(table.lookup(addr1).unwrap(), Destination { next_hop: addr1, device: device1 });
        assert_eq!(table.lookup(addr2).unwrap(), Destination { next_hop: addr5, device: device0 });
        assert_eq!(table.lookup(addr5).unwrap(), Destination { next_hop: addr5, device: device0 });

        // Add the following routes:
        //   sub5/-23 -> device1
        //
        // Our expected forwarding table should look like:
        //  sub2/-24 -> addr5 w/ device1
        //  sub4/-24 -> addr5 w/ device1
        //  sub5/-24 -> device0
        //  sub1/-24 -> device1
        //  sub5/-23 -> device1
        //
        // addr5 should now prefer sub5/-23 over sub5/-24 for addressing as it is a more specific
        // subnet.
        let sub5_p23 = sub::<I>(5, 23);
        table.add_device_route(sub5_p23, device1).unwrap();
        table.print();
        assert_eq!(table.iter_active().count(), 5);
        assert_eq!(table.iter_installed().count(), 7);
        assert!(table.iter_active().any(|x| (x.subnet == sub2_s24)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr5, device: device1 }
                })));
        assert!(table.iter_active().any(|x| (x.subnet == sub4_s24)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr5, device: device1 }
                })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub5_s24)
                && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub1_s24)
                && (x.dest == ActiveEntryDest::Local { device: device1 })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub5_p23)
                && (x.dest == ActiveEntryDest::Local { device: device1 })));
        assert_eq!(table.lookup(addr1).unwrap(), Destination { next_hop: addr1, device: device1 });
        assert_eq!(table.lookup(addr2).unwrap(), Destination { next_hop: addr5, device: device1 });
        assert_eq!(table.lookup(addr5).unwrap(), Destination { next_hop: addr5, device: device1 });
    }

    #[ip_test]
    fn test_use_most_specific_route_ip<I: Ip>() {
        let mut table = ForwardingTable::<I, DeviceId>::default();
        let device0 = DeviceId::new_ethernet(0);
        let device1 = DeviceId::new_ethernet(1);
        let (addr7, sub7_s24) = next_hop_addr_sub::<I>(7, 24);
        let (addr8, sub8_s27) = next_hop_addr_sub::<I>(8, 27);
        let (addr10, sub10_s25) = next_hop_addr_sub::<I>(10, 25);
        let (addr12, sub12_s26) = next_hop_addr_sub::<I>(12, 26);
        let (addr14, sub14_s25) = next_hop_addr_sub::<I>(14, 25);
        let (addr15, _) = next_hop_addr_sub::<I>(15, 24);

        // In the following comments, we will used a modified form of prefix notation.
        // Normally to identify the prefix of an address, we do ADDRESS/PREFIX (e.g.
        // fe80::e80c:830f:1cc3:2336/64 for IPv6; 100.96.232.33/24 for IPv4).
        // Here, we will do ADDRESS/-SUFFIX to represent the number of bits of the
        // host portion of the address instead of the network. The following is the
        // relationship between SUFFIX and PREFIX: PREFIX = ADDRESS_BITS - SUFFIX.
        // So for the examples given earlier:
        //  fe80::e80c:830f:1cc3:2336/64 <-> fe80::e80c:830f:1cc3:2336/-64
        //  100.96.232.33/24 <-> 100.96.232.33/-8
        //
        // We do this because this method is generic for IPv4 and IPv6 which have different
        // address lengths. To keep the comments consistent (at the cost of some readability)
        // we use this custom notation for the comments below.
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
        table.print();
        assert_eq!(table.iter_active().count(), 1);
        assert_eq!(table.iter_installed().count(), 1);
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub8_s27)
                && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert!(table.lookup(addr7).is_none());
        assert_eq!(table.lookup(addr8).unwrap(), Destination { next_hop: addr8, device: device0 });
        assert_eq!(
            table.lookup(addr10).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );
        assert_eq!(
            table.lookup(addr12).unwrap(),
            Destination { next_hop: addr12, device: device0 }
        );
        assert_eq!(
            table.lookup(addr14).unwrap(),
            Destination { next_hop: addr14, device: device0 }
        );
        assert_eq!(
            table.lookup(addr15).unwrap(),
            Destination { next_hop: addr15, device: device0 }
        );

        // Add the following routes:
        //  sub12/-26 -> device1
        //
        // Our expected forwarding table should look like:
        //  sub8/-27 -> device0
        //  sub12/-26 -> device1

        table.add_device_route(sub12_s26, device1).unwrap();
        table.print();
        assert_eq!(table.iter_active().count(), 2);
        assert_eq!(table.iter_installed().count(), 2);
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub8_s27)
                && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub12_s26)
                && (x.dest == ActiveEntryDest::Local { device: device1 })));
        assert!(table.lookup(addr7).is_none());
        assert_eq!(table.lookup(addr8).unwrap(), Destination { next_hop: addr8, device: device0 });
        assert_eq!(
            table.lookup(addr10).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );
        assert_eq!(
            table.lookup(addr12).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(addr14).unwrap(),
            Destination { next_hop: addr14, device: device1 }
        );
        assert_eq!(
            table.lookup(addr15).unwrap(),
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
        table.print();
        assert_eq!(table.iter_active().count(), 3);
        assert_eq!(table.iter_installed().count(), 3);
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub8_s27)
                && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub12_s26)
                && (x.dest == ActiveEntryDest::Local { device: device1 })));
        assert!(table.iter_active().any(|x| (x.subnet == sub14_s25)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr10, device: device0 }
                })));
        assert_eq!(table.lookup(addr8).unwrap(), Destination { next_hop: addr8, device: device0 });
        assert_eq!(
            table.lookup(addr10).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );
        assert_eq!(
            table.lookup(addr12).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(addr14).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );
        assert_eq!(
            table.lookup(addr15).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );

        //
        // This next two tests are important.
        //
        // Here, we add a route from sub10/-25 -> addr7. The routing table as no route from addr7
        // so normally we would not have any route to the subnet sub10/-25. However, we have a
        // route for a less specific subnet (sub8/-27) which IS routable so we use that instead.
        //
        // When we do eventually make sub7/-24 routable, sub10/-25 will be routed through addr7.
        //

        // Add the following routes:
        //  sub10/-25 -> addr7
        //
        // Our expected forwarding table should look like:
        //  sub8/-27 -> device0
        //  sub12/-26 -> device1
        //  sub14/-25 -> addr10 w/ device0

        table.add_route(sub10_s25, addr7).unwrap();
        table.print();
        assert_eq!(table.iter_active().count(), 3);
        assert_eq!(table.iter_installed().count(), 4);
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub8_s27)
                && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub12_s26)
                && (x.dest == ActiveEntryDest::Local { device: device1 })));
        assert!(table.iter_active().any(|x| (x.subnet == sub14_s25)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr10, device: device0 }
                })));
        assert!(table.lookup(addr7).is_none());
        assert_eq!(table.lookup(addr8).unwrap(), Destination { next_hop: addr8, device: device0 });
        assert_eq!(
            table.lookup(addr10).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );
        assert_eq!(
            table.lookup(addr12).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(addr14).unwrap(),
            Destination { next_hop: addr10, device: device0 }
        );
        assert_eq!(
            table.lookup(addr15).unwrap(),
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
        table.print();
        assert_eq!(table.iter_active().count(), 5);
        assert_eq!(table.iter_installed().count(), 5);
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub8_s27)
                && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub12_s26)
                && (x.dest == ActiveEntryDest::Local { device: device1 })));
        assert!(table.iter_active().any(|x| (x.subnet == sub14_s25)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr12, device: device1 }
                })));
        assert!(table.iter_active().any(|x| (x.subnet == sub7_s24)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr12, device: device1 }
                })));
        assert!(table.iter_active().any(|x| (x.subnet == sub10_s25)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr12, device: device1 }
                })));
        assert_eq!(table.lookup(addr7).unwrap(), Destination { next_hop: addr12, device: device1 });
        assert_eq!(table.lookup(addr8).unwrap(), Destination { next_hop: addr8, device: device0 });
        assert_eq!(
            table.lookup(addr10).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(addr12).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(addr14).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(addr15).unwrap(),
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
        table.print();
        assert_eq!(table.iter_active().count(), 5);
        assert_eq!(table.iter_installed().count(), 6);
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub8_s27)
                && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub12_s26)
                && (x.dest == ActiveEntryDest::Local { device: device1 })));
        assert!(table.iter_active().any(|x| (x.subnet == sub7_s24)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr12, device: device1 }
                })));
        assert!(table.iter_active().any(|x| (x.subnet == sub10_s25)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr12, device: device1 }
                })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub14_s25)
                && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert_eq!(table.lookup(addr7).unwrap(), Destination { next_hop: addr12, device: device1 });
        assert_eq!(table.lookup(addr8).unwrap(), Destination { next_hop: addr8, device: device0 });
        assert_eq!(
            table.lookup(addr10).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(addr12).unwrap(),
            Destination { next_hop: addr12, device: device1 }
        );
        assert_eq!(
            table.lookup(addr14).unwrap(),
            Destination { next_hop: addr14, device: device0 }
        );
        assert_eq!(
            table.lookup(addr15).unwrap(),
            Destination { next_hop: addr15, device: device0 }
        );

        // Check the installed table just in case.
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub8_s27) && (x.dest == EntryDest::Local { device: device0 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub12_s26) && (x.dest == EntryDest::Local { device: device1 })));
        assert!(
            table
                .iter_installed()
                .any(|x| (x.subnet == sub14_s25)
                    && (x.dest == EntryDest::Remote { next_hop: addr10 }))
        );
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub10_s25) && (x.dest == EntryDest::Remote { next_hop: addr7 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub7_s24) && (x.dest == EntryDest::Remote { next_hop: addr12 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub14_s25) && (x.dest == EntryDest::Local { device: device0 })));
    }

    #[ip_test]
    fn test_cycle_ip<I: Ip>() {
        let mut table = ForwardingTable::<I, DeviceId>::default();
        let device0 = DeviceId::new_ethernet(0);
        let (addr1, sub1) = next_hop_addr_sub::<I>(1, 24);
        let (addr2, sub2) = next_hop_addr_sub::<I>(2, 24);
        let (addr3, sub3) = next_hop_addr_sub::<I>(3, 24);
        let (addr4, sub4) = next_hop_addr_sub::<I>(4, 24);
        let (addr5, _) = next_hop_addr_sub::<I>(5, 24);

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
        table.print();
        assert_eq!(table.iter_active().count(), 1);
        assert_eq!(table.iter_installed().count(), 5);
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub2) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Remote { next_hop: addr4 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub4) && (x.dest == EntryDest::Remote { next_hop: addr5 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Local { device: device0 })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub3) && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert!(table.lookup(addr1).is_none());
        assert!(table.lookup(addr2).is_none());
        assert_eq!(table.lookup(addr3).unwrap(), Destination { next_hop: addr3, device: device0 });
        assert!(table.lookup(addr4).is_none());
        assert!(table.lookup(addr5).is_none());

        // Keep the route with the cycle, but add another route that doesn't have a cycle
        // for sub2.
        //
        // Add the following routes:
        //  sub2 -> addr3
        //
        // Our expected forwarding table should look like:
        //  sub3 -> device0
        //  sub1 -> addr3 w/ device0
        //  sub2 -> addr3 w/ device0

        table.add_route(sub2, addr3).unwrap();
        table.print();
        assert_eq!(table.iter_active().count(), 3);
        assert_eq!(table.iter_installed().count(), 6);
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub2) && (x.dest == EntryDest::Remote { next_hop: addr2 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Remote { next_hop: addr4 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub4) && (x.dest == EntryDest::Remote { next_hop: addr5 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub3) && (x.dest == EntryDest::Local { device: device0 })));
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub2) && (x.dest == EntryDest::Remote { next_hop: addr3 })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub3) && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert!(table.iter_active().any(|x| (x.subnet == sub1)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr3, device: device0 }
                })));
        assert!(table.iter_active().any(|x| (x.subnet == sub2)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr3, device: device0 }
                })));
        assert_eq!(table.lookup(addr1).unwrap(), Destination { next_hop: addr3, device: device0 });
        assert_eq!(table.lookup(addr2).unwrap(), Destination { next_hop: addr3, device: device0 });
        assert_eq!(table.lookup(addr3).unwrap(), Destination { next_hop: addr3, device: device0 });
        assert!(table.lookup(addr4).is_none());
        assert!(table.lookup(addr5).is_none());
    }

    #[ip_test]
    fn test_default_route_ip<I: Ip>() {
        let mut table = ForwardingTable::<I, DeviceId>::default();
        let device0 = DeviceId::new_ethernet(0);
        let (addr1, sub1) = next_hop_addr_sub::<I>(1, 24);
        let (addr2, _) = next_hop_addr_sub::<I>(2, 24);
        let (addr3, _) = next_hop_addr_sub::<I>(3, 24);

        // Add the following routes:
        //  sub1 -> device0
        //
        // Our expected forwarding table should look like:
        //  sub1 -> device0

        table.add_device_route(sub1, device0).unwrap();
        table.print();
        assert_eq!(table.iter_active().count(), 1);
        assert_eq!(table.iter_installed().count(), 1);
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Local { device: device0 })));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub1) && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert_eq!(table.lookup(addr1).unwrap(), Destination { next_hop: addr1, device: device0 });
        assert!(table.lookup(addr2).is_none());

        // Add a default route.
        //
        // Our expected forwarding table should look like:
        //  sub1 -> device0
        //  default -> addr1 w/ device0

        let default_sub = Subnet::new(I::UNSPECIFIED_ADDRESS, 0).unwrap();
        table.add_route(default_sub, addr1).unwrap();
        assert_eq!(table.iter_active().count(), 2);
        assert_eq!(table.iter_installed().count(), 2);
        assert!(table
            .iter_installed()
            .any(|x| (x.subnet == sub1) && (x.dest == EntryDest::Local { device: device0 })));
        assert!(table.iter_installed().any(
            |x| (x.subnet == default_sub) && (x.dest == EntryDest::Remote { next_hop: addr1 })
        ));
        assert!(table
            .iter_active()
            .any(|x| (x.subnet == sub1) && (x.dest == ActiveEntryDest::Local { device: device0 })));
        assert!(table.iter_active().any(|x| (x.subnet == default_sub)
            && (x.dest
                == ActiveEntryDest::Remote {
                    dest: Destination { next_hop: addr1, device: device0 }
                })));
        assert_eq!(table.lookup(addr1).unwrap(), Destination { next_hop: addr1, device: device0 });
        assert_eq!(table.lookup(addr2).unwrap(), Destination { next_hop: addr1, device: device0 });
        assert_eq!(table.lookup(addr3).unwrap(), Destination { next_hop: addr1, device: device0 });
    }
}
