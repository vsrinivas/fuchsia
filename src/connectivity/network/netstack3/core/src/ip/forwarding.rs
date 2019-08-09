// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::{self, Debug, Formatter};

use log::warn;
use net_types::ip::{Ip, Subnet};

use crate::device::DeviceId;
use crate::ip::*;

// TODO(joshlf):
// - How do we detect circular routes? Do we attempt to detect at rule
//   installation time? At runtime? Using what algorithm?

// NOTE on loopback addresses: Loopback addresses should be handled before
// reaching the forwarding table. For that reason, we do not prevent a rule
// whose subnet is a subset of the loopback subnet from being installed; they
// will never get triggered anyway, so implementing the logic of detecting these
// rules is a needless complexity.

const MAX_RECURSE_DEPTH: u8 = 16;

/// The destination of an outbound IP packet.
///
/// Outbound IP packets are sent to a particular device (specified by the
/// `device` field). They are sent to a particular IP host on the local network
/// attached to that device, identified by `next_hop`. Note that `next_hop` is
/// not necessarily the destination IP address of the IP packet. In particular,
/// if the destination is not on the local network, the `next_hop` will be the
/// IP address of the next IP router on the way to the destination.
#[derive(PartialEq, Eq)]
pub(crate) struct Destination<I: Ip> {
    pub(crate) next_hop: I::Addr,
    pub(crate) device: DeviceId,
}

impl<I: Ip> Debug for Destination<I> {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        // This is the same format we'd get using #[derive(Debug)], but that
        // would require that I: Debug, which doesn't hold for all I: Ip.
        f.debug_struct("Destination")
            .field("next_hop", &self.next_hop)
            .field("device", &self.device)
            .finish()
    }
}

/// An IP forwarding table.
///
/// `ForwardingTable` maps destination subnets to the nearest IP hosts (on the
/// local network) able to route IP packets to those subnets.
#[derive(Default)]
pub(crate) struct ForwardingTable<I: Ip> {
    entries: Vec<Entry<I::Addr>>,
}

impl<I: Ip> ForwardingTable<I> {
    /// Do we already have the route to a subnet in our forwarding table?
    ///
    /// Note, this method will return `true` if we already hold a route
    /// to the same subnet.
    fn contains_entry(&self, entry: &Entry<I::Addr>) -> bool {
        self.entries.iter().any(|e| e.subnet == entry.subnet)
    }

    /// Adds `entry` to the forwarding table if it does not already exist.
    fn add_entry(&mut self, entry: Entry<I::Addr>) -> Result<(), ExistsError> {
        if self.contains_entry(&entry) {
            Err(ExistsError)
        } else {
            self.entries.push(entry);
            Ok(())
        }
    }

    pub(crate) fn add_route(
        &mut self,
        subnet: Subnet<I::Addr>,
        next_hop: I::Addr,
    ) -> Result<(), ExistsError> {
        debug!("adding route: {} -> {}", subnet, next_hop);
        self.add_entry(Entry { subnet, dest: EntryDest::Remote { next_hop } })
    }

    pub(crate) fn add_device_route(
        &mut self,
        subnet: Subnet<I::Addr>,
        device: DeviceId,
    ) -> Result<(), ExistsError> {
        debug!("adding device route: {} -> {}", subnet, device);
        self.add_entry(Entry { subnet, dest: EntryDest::Local { device } })
    }

    /// Delete a route from the forwarding table, returning `Err` if
    /// no route was found to be deleted.
    pub(crate) fn del_route(&mut self, subnet: Subnet<I::Addr>) -> Result<(), NotFoundError> {
        debug!("deleting route: {}", subnet);

        let old_len = self.entries.len();
        let pos = self.entries.retain(|entry| entry.subnet != subnet);
        let new_len = self.entries.len();

        if old_len == new_len {
            Err(NotFoundError)
        } else {
            assert_eq!(old_len - new_len, 1);
            Ok(())
        }
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
    pub(crate) fn lookup(&self, address: I::Addr) -> Option<Destination<I>> {
        assert!(
            !I::LOOPBACK_SUBNET.contains(&address),
            "loopback addresses should be handled before consulting the forwarding table"
        );

        if !address.is_unspecified() {
            let dst = self.lookup_helper(address, 0);
            trace!("lookup({}) -> {:?}", address, dst);
            dst
        } else {
            None
        }
    }

    pub(crate) fn iter_routes(&self) -> std::slice::Iter<Entry<I::Addr>> {
        self.entries.iter()
    }

    fn lookup_helper(&self, address: I::Addr, depth: u8) -> Option<Destination<I>> {
        if depth > MAX_RECURSE_DEPTH {
            warn!("forwarding table lookup hit recursion depth limit");
            return None;
        }

        let best_match = self
            .entries
            .iter()
            .filter(|e| e.subnet.contains(&address))
            .max_by_key(|e| e.subnet.prefix());

        match best_match {
            Some(Entry { dest: EntryDest::Local { device }, .. }) => {
                Some(Destination { next_hop: address, device: *device })
            }
            Some(Entry { dest: EntryDest::Remote { next_hop }, .. }) => {
                self.lookup_helper(*next_hop, depth + 1)
            }
            None => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::testutil::get_dummy_config;

    fn test_add_del_lookup_simple_ip<I: Ip>() {
        let mut table = ForwardingTable::<I>::default();

        let config = get_dummy_config::<I::Addr>();
        let subnet = config.subnet;
        let device = DeviceId::new_ethernet(0);

        // Should add the route successfully.
        table.add_device_route(subnet, device).unwrap();

        // Attempting to add the route again should fail.
        assert_eq!(table.add_device_route(subnet, device).unwrap_err(), ExistsError);

        // Add the route but as a next hop route. Should still fail since we have a destination
        // to subnet.
        #[specialize_ip]
        fn next_hop_addr<I: Ip>() -> I::Addr {
            #[ipv4]
            return Ipv4Addr::new([10, 0, 0, 1]);

            #[ipv6]
            return Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 1]);
        }
        let next_hop = next_hop_addr::<I>();
        assert_eq!(table.add_route(subnet, next_hop).unwrap_err(), ExistsError);

        // Delete the device route.
        table.del_route(subnet).unwrap();

        // Add the next hop route.
        table.add_route(subnet, next_hop).unwrap();

        // Attempting to add the next hop route again should fail.
        assert_eq!(table.add_route(subnet, next_hop).unwrap_err(), ExistsError);

        // Add a device route from the `next_hop` to some device.
        let next_hop_specific_subnet = Subnet::new(next_hop, I::Addr::BYTES * 8).unwrap();
        table.add_device_route(next_hop_specific_subnet, device).unwrap();

        // Check the current state of the forwarding table.
        assert_eq!(table.iter_routes().count(), 2);
        assert!(table
            .iter_routes()
            .any(|x| (x.subnet == subnet) && (x.dest == EntryDest::Remote { next_hop })));
        assert!(table
            .iter_routes()
            .any(|x| (x.subnet == next_hop_specific_subnet)
                && (x.dest == EntryDest::Local { device })));

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

    #[test]
    fn test_add_del_lookup_simple_ipv4() {
        test_add_del_lookup_simple_ip::<Ipv4>();
    }

    #[test]
    fn test_add_del_lookup_simple_ipv6() {
        test_add_del_lookup_simple_ip::<Ipv6>();
    }
}
