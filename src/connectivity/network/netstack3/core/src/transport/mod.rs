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

use crate::transport::udp::UdpEventDispatcher;
use crate::{Context, EventDispatcher};

/// The state associated with the transport layer.
pub(crate) struct TransportLayerState<D: EventDispatcher> {
    tcp: self::tcp::TcpState,
    udp: self::udp::UdpState<D>,
}

impl<D: EventDispatcher> Default for TransportLayerState<D> {
    fn default() -> TransportLayerState<D> {
        TransportLayerState {
            tcp: self::tcp::TcpState::default(),
            udp: self::udp::UdpState::default(),
        }
    }
}

/// The identifier for timer events in the transport layer.
#[derive(Copy, Clone, PartialEq)]
pub(crate) enum TransportLayerTimerId {
    /// A timer event in the TCP layer
    Tcp(tcp::TcpTimerId),
}

/// Handle a timer event firing in the transport layer.
pub(crate) fn handle_timeout<D: EventDispatcher>(ctx: &mut Context<D>, id: TransportLayerTimerId) {
    unimplemented!()
}

/// An event dispatcher for the transport layer.
///
/// See the `EventDispatcher` trait in the crate root for more details.
pub trait TransportLayerEventDispatcher: UdpEventDispatcher {}

/// A bidirectional map between listeners and addresses.
///
/// A `ListenerAddrMap` maps a listener object (`L`) to zero or more address
/// objects (`A`). It allows for constant-time address -> listener and listener
/// -> address lookup, and insertion and deletion based on listener.
struct ListenerAddrMap<L, A> {
    listener_to_addrs: HashMap<L, Vec<A>>,
    addr_to_listener: HashMap<A, L>,
}

impl<L: Eq + Hash + Clone, A: Eq + Hash + Clone> ListenerAddrMap<L, A> {
    fn insert(&mut self, listener: L, addrs: Vec<A>) {
        for addr in &addrs {
            self.addr_to_listener.insert(addr.clone(), listener.clone());
        }
        self.listener_to_addrs.insert(listener, addrs);
    }
}

impl<L: Eq + Hash, A: Eq + Hash> ListenerAddrMap<L, A> {
    fn get_by_addr(&self, addr: &A) -> Option<&L> {
        self.addr_to_listener.get(addr)
    }

    fn get_by_listener(&self, listener: &L) -> Option<&Vec<A>> {
        self.listener_to_addrs.get(listener)
    }

    fn remove_by_listener(&mut self, listener: &L) -> Option<Vec<A>> {
        let addrs = self.listener_to_addrs.remove(listener)?;
        for addr in &addrs {
            self.addr_to_listener.remove(addr).unwrap();
        }
        Some(addrs)
    }
}

impl<L: Eq + Hash, A: Eq + Hash> Default for ListenerAddrMap<L, A> {
    fn default() -> ListenerAddrMap<L, A> {
        ListenerAddrMap {
            listener_to_addrs: HashMap::default(),
            addr_to_listener: HashMap::default(),
        }
    }
}

/// A bidirectional map between connections and addresses.
///
/// A `ConnAddrMap` maps a connection object (`C`) to an address object (`A`).
/// It allows for constant-time addres -> connection and connection -> address
/// lookup, and insertion and deletion based on connection.
///
/// It differs from a `ListenerAddrMap` in that only a single address per
/// connection is supported.
struct ConnAddrMap<C, A> {
    conn_to_addr: HashMap<C, A>,
    addr_to_conn: HashMap<A, C>,
}

impl<C: Eq + Hash + Clone, A: Eq + Hash + Clone> ConnAddrMap<C, A> {
    fn insert(&mut self, conn: C, addr: A) {
        self.addr_to_conn.insert(addr.clone(), conn.clone());
        self.conn_to_addr.insert(conn, addr);
    }
}

impl<C: Eq + Hash, A: Eq + Hash> ConnAddrMap<C, A> {
    fn get_by_addr(&self, addr: &A) -> Option<&C> {
        self.addr_to_conn.get(addr)
    }

    fn get_by_conn(&self, conn: &C) -> Option<&A> {
        self.conn_to_addr.get(conn)
    }

    fn remove_by_conn(&mut self, conn: &C) -> Option<A> {
        let addr = self.conn_to_addr.remove(conn)?;
        self.addr_to_conn.remove(&addr).unwrap();
        Some(addr)
    }
}

impl<A: Eq + Hash, C: Eq + Hash> Default for ConnAddrMap<A, C> {
    fn default() -> ConnAddrMap<A, C> {
        ConnAddrMap { conn_to_addr: HashMap::default(), addr_to_conn: HashMap::default() }
    }
}
