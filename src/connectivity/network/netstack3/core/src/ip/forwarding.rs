// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::{self, Debug, Formatter};

use crate::device::DeviceId;
use crate::ip::*;

// TODO(joshlf):
// - Implement route deletion.
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
    pub(crate) fn add_route(
        &mut self,
        subnet: Subnet<I::Addr>,
        next_hop: I::Addr,
    ) -> Result<(), ExistsError> {
        debug!("adding route: {} -> {}", subnet, next_hop);
        if self.entries.iter().any(|entry| entry.subnet == subnet) {
            Err(ExistsError)
        } else {
            self.entries.push(Entry { subnet, dest: EntryDest::Remote { next_hop } });
            Ok(())
        }
    }

    pub(crate) fn add_device_route(
        &mut self,
        subnet: Subnet<I::Addr>,
        device: DeviceId,
    ) -> Result<(), ExistsError> {
        debug!("adding device route: {} -> {}", subnet, device);
        if self.entries.iter().any(|entry| entry.subnet == subnet) {
            Err(ExistsError)
        } else {
            self.entries.push(Entry { subnet, dest: EntryDest::Local { device } });
            Ok(())
        }
    }

    /// Delete a route from the forwarding table, returning `Err` if
    /// no route was found to be deleted.
    pub(crate) fn del_route(&mut self, subnet: Subnet<I::Addr>) -> Result<(), NotFoundError> {
        debug!("deleting route: {}", subnet);
        let pos = self.entries.iter().position(|entry| entry.subnet == subnet);
        match pos {
            Some(pos) => {
                self.entries.remove(pos);
                Ok(())
            }
            None => Err(NotFoundError),
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
    /// # Panics
    ///
    /// `lookup` asserts that `address` is not in the loopback interface.
    /// Traffic destined for loopback addresses from local applications should
    /// be properly routed without consulting the forwarding table, and traffic
    /// from the network with a loopback destination address is invalid and
    /// should be dropped before consulting the forwarding table.
    pub(crate) fn lookup(&self, address: I::Addr) -> Option<Destination<I>> {
        assert!(
            !I::LOOPBACK_SUBNET.contains(address),
            "loopback addresses should be handled before consulting the forwarding table"
        );

        let dst = self.lookup_helper(address);
        trace!("lookup({}) -> {:?}", address, dst);
        dst
    }

    pub(crate) fn iter_routes(&self) -> std::slice::Iter<Entry<I::Addr>> {
        self.entries.iter()
    }

    fn lookup_helper(&self, address: I::Addr) -> Option<Destination<I>> {
        let best_match = self
            .entries
            .iter()
            .filter(|e| e.subnet.contains(address))
            .max_by_key(|e| e.subnet.prefix());

        match best_match {
            Some(Entry { dest: EntryDest::Local { device }, .. }) => {
                Some(Destination { next_hop: address, device: *device })
            }
            Some(Entry { dest: EntryDest::Remote { next_hop }, .. }) => {
                self.lookup_helper(*next_hop)
            }
            None => None,
        }
    }
}
