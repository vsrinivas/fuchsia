// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The transport layer.
//!
//! # Listeners and connections
//!
//! Some transport layer protocols (notably TCP and UDP) follow a common pattern
//! with respect to registering listeners and connections. There are some
//! subtleties here that are worth pointing out.
//!
//! ## Connections
//!
//! A connection has simpler semantics than a listener. It is bound to a single
//! local address and port and a single remote address and port. By virtue of
//! being bound to a local address, it is also bound to a local interface. This
//! means that, regardless of the entries in the forwarding table, all traffic
//! on that connection will always egress over the same interface. [^1] This also
//! means that, if the interface's address changes, any connections bound to it
//! are severed.
//!
//! ## Listeners
//!
//! A listener, on the other hand, can be bound to any number of local addresses
//! (although it is still always bound to a particular port). From the
//! perspective of this crate, there are two ways of registering a listener:
//! - By specifying one or more local addresses, the listener will be bound to
//!   each of those local addresses.
//! - By specifying zero local addresses, the listener will be bound to all
//!   addresses. These are referred to in our documentation as "wildcard
//!   listeners".
//!
//! The algorithm for figuring out what listener to deliver a packet to is as
//! follows: If there is any listener bound to the specific local address and
//! port addressed in the packet, deliver the packet to that listener.
//! Otherwise, if there is a wildcard listener bound the port addressed in the
//! packet, deliver the packet to that listener. This implies that if a listener
//! is removed which was bound to a particular local address, it can "uncover" a
//! wildcard listener bound to the same port, allowing traffic which would
//! previously have been delivered to the normal listener to now be delivered to
//! the wildcard listener.
//!
//! If desired, clients of this crate can implement a different mechanism for
//! registering listeners on all local addresses - enumerate every local
//! address, and then specify all of the local addresses when registering the
//! listener. This approach will not support shadowing, as a different listener
//! binding to the same port will explicitly conflict with the existing
//! listener, and will thus be rejected. In other words, from the perspective of
//! this crate's API, such listeners will appear like normal listeners that just
//! happen to bind all of the addresses, rather than appearing like wildcard
//! listeners.
//!
//! [^1]: It is an open design question as to whether incoming traffic on the
//!       connection will be accepted from a different interface. This is part
//!       of the "weak host model" vs "strong host model" discussion.

pub(crate) mod tcp;
pub(crate) mod udp;

use std::collections::HashMap;
use std::hash::Hash;

use net_types::ip::{Ipv4, Ipv6};

use crate::data_structures::IdMap;
use crate::ip::icmp::IcmpIpExt;
use crate::transport::udp::UdpEventDispatcher;
use crate::{Context, EventDispatcher};

/// The state associated with the transport layer.
pub(crate) struct TransportLayerState {
    udpv4: self::udp::UdpState<Ipv4>,
    udpv6: self::udp::UdpState<Ipv6>,
}

impl Default for TransportLayerState {
    fn default() -> TransportLayerState {
        TransportLayerState {
            udpv4: self::udp::UdpState::default(),
            udpv6: self::udp::UdpState::default(),
        }
    }
}

/// The identifier for timer events in the transport layer.
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub(crate) enum TransportLayerTimerId {}

/// Handle a timer event firing in the transport layer.
pub(crate) fn handle_timeout<D: EventDispatcher>(_ctx: &mut Context<D>, id: TransportLayerTimerId) {
    match id {}
}

/// An event dispatcher for the transport layer.
///
/// See the `EventDispatcher` trait in the crate root for more details.
pub trait TransportLayerEventDispatcher<I: IcmpIpExt>: UdpEventDispatcher<I> {}

/// A bidirectional map between listeners and addresses.
///
/// A `ListenerAddrMap` keeps addresses mapped by integer indexes, and allows
/// for constant-time mapping in either direction (though address -> index
/// mappings are via a hash map, and are thus slower).
struct ListenerAddrMap<A> {
    listener_to_addrs: IdMap<Vec<A>>,
    addr_to_listener: HashMap<A, usize>,
}

impl<A: Eq + Hash + Clone> ListenerAddrMap<A> {
    fn insert(&mut self, addrs: Vec<A>) -> usize {
        let listener = self.listener_to_addrs.push(addrs.clone());
        for addr in &addrs {
            self.addr_to_listener.insert(addr.clone(), listener);
        }
        listener
    }
}

impl<A: Eq + Hash> ListenerAddrMap<A> {
    fn get_by_addr(&self, addr: &A) -> Option<usize> {
        self.addr_to_listener.get(addr).cloned()
    }

    fn get_by_listener(&self, listener: usize) -> Option<&Vec<A>> {
        self.listener_to_addrs.get(listener)
    }

    fn remove_by_listener(&mut self, listener: usize) -> Option<Vec<A>> {
        let addrs = self.listener_to_addrs.remove(listener)?;
        for addr in &addrs {
            self.addr_to_listener.remove(addr).unwrap();
        }
        Some(addrs)
    }
}

impl<A: Eq + Hash> Default for ListenerAddrMap<A> {
    fn default() -> ListenerAddrMap<A> {
        ListenerAddrMap {
            listener_to_addrs: IdMap::default(),
            addr_to_listener: HashMap::default(),
        }
    }
}

/// A bidirectional map between connections and addresses.
///
/// A `ConnAddrMap` keeps addresses mapped by integer indexes, and allows for
/// constant-time mapping in either direction (though address -> index mappings
/// are via a hash map, and thus slower).
///
/// It differs from a `ListenerAddrMap` in that only a single address per
/// connection is supported.
pub(crate) struct ConnAddrMap<A, C = A> {
    id_to_conn: IdMap<C>,
    addr_to_id: HashMap<A, usize>,
}

impl<A: Eq + Hash + Clone, C> ConnAddrMap<A, C> {
    pub(crate) fn insert(&mut self, addr: A, conn: C) -> usize {
        let id = self.id_to_conn.push(conn);
        self.addr_to_id.insert(addr, id);
        id
    }
}

impl<A: Eq + Hash, C> ConnAddrMap<A, C> {
    pub(crate) fn get_id_by_addr(&self, addr: &A) -> Option<usize> {
        self.addr_to_id.get(addr).cloned()
    }

    pub(crate) fn get_conn_by_id(&self, id: usize) -> Option<&C> {
        self.id_to_conn.get(id)
    }
}

impl<A: Eq + Hash, C> ConnAddrMap<A, C>
where
    for<'a> &'a C: Into<A>,
{
    fn remove_by_id(&mut self, id: usize) -> Option<C> {
        let conn = self.id_to_conn.remove(id)?;
        self.addr_to_id.remove(&(&conn).into()).unwrap();
        Some(conn)
    }

    /// Update the elements of the map in-place, retaining only the elements for
    /// which `f` returns true.
    ///
    /// `update_return` invokes `f` on each element of the map, and removes from
    /// the map those elements for which `f` returns false. The return value is
    /// an iterator over the removed elements.
    ///
    /// WARNING: The mutation will only occur when the returned iterator is
    /// executed. Simply calling `update_retain` and then discarding the return
    /// value will do nothing!
    // TODO(rheacock): remove `allow(dead_code)` when this is used.
    #[allow(dead_code)]
    pub(crate) fn update_retain<'a, F: 'a + Fn(&mut C) -> bool>(
        &'a mut self,
        f: F,
    ) -> impl 'a + Iterator<Item = (usize, C)> {
        let addr_to_id = &mut self.addr_to_id;
        self.id_to_conn.update_retain(f).map(move |(id, conn)| {
            addr_to_id.remove(&(&conn).into()).unwrap();
            (id, conn)
        })
    }
}

impl<A: Eq + Hash, C> Default for ConnAddrMap<A, C> {
    fn default() -> ConnAddrMap<A, C> {
        ConnAddrMap { id_to_conn: IdMap::default(), addr_to_id: HashMap::default() }
    }
}
