// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! General-purpose socket utilities common to device layer and IP layer
//! sockets.

use alloc::{collections::HashMap, vec::Vec};
use core::hash::Hash;

use crate::data_structures::IdMap;

/// A socket providing the ability to communicate with a remote or local host.
///
/// A `Socket` is a long-lived object which provides the ability to either send
/// outbound traffic to or receive inbound traffic from a particular remote or
/// local host or set of hosts.
///
/// `Socket`s may cache certain routing information that is used to speed up the
/// operation of sending outbound packets. However, this means that updates to
/// global state (for example, updates to the forwarding table, to the neighbor
/// cache, etc) may invalidate that cached information. Thus, certain updates
/// may require updating all stored sockets as well. See the `Update` and
/// `UpdateMeta` associated types and the `apply_update` method for more
/// details.
pub trait Socket {
    /// The type of updates to the socket.
    ///
    /// Updates are emitted whenever information changes that might require
    /// information cached in sockets to be updated. For example, for IP
    /// sockets, changes to the forwarding table might require that an IP
    /// socket's outbound device be updated.
    type Update;

    /// Metadata required to perform an update.
    ///
    /// Extra metadata may be required in order to apply an update to a socket.
    type UpdateMeta;

    /// The type of errors that can occur while performing an update.
    type UpdateError;

    /// Apply an update to the socket.
    ///
    /// `apply_update` applies the given update, possibly changing cached
    /// information. If it returns `Err`, then the socket MUST be closed. This
    /// is a MUST, not a SHOULD, as the cached information may now be invalid,
    /// and the behavior of any further use of the socket may now be
    /// unspecified. It is the caller's responsibility to ensure that the socket
    /// is no longer used, including instructing the bindings not to use it
    /// again (if applicable).
    fn apply_update(
        &mut self,
        update: &Self::Update,
        meta: &Self::UpdateMeta,
    ) -> Result<(), Self::UpdateError>;
}

/// A bidirectional map between listener sockets and addresses.
///
/// A `ListenerSocketMap` keeps addresses mapped by integer indexes, and allows
/// for constant-time mapping in either direction (though address -> index
/// mappings are via a hash map, and are thus slower).
pub(crate) struct ListenerSocketMap<A> {
    listener_to_addrs: IdMap<Vec<A>>,
    addr_to_listener: HashMap<A, usize>,
}

impl<A: Eq + Hash + Clone> ListenerSocketMap<A> {
    pub(crate) fn insert(&mut self, addrs: Vec<A>) -> usize {
        let listener = self.listener_to_addrs.push(addrs.clone());
        for addr in addrs.into_iter() {
            assert_eq!(self.addr_to_listener.insert(addr, listener), None);
        }
        listener
    }
}

impl<A: Eq + Hash> ListenerSocketMap<A> {
    pub(crate) fn get_by_addr(&self, addr: &A) -> Option<usize> {
        self.addr_to_listener.get(addr).cloned()
    }

    pub(crate) fn get_by_listener(&self, listener: usize) -> Option<&Vec<A>> {
        self.listener_to_addrs.get(listener)
    }

    pub(crate) fn iter_addrs(&self) -> impl Iterator<Item = &'_ A> + ExactSizeIterator {
        self.addr_to_listener.keys()
    }

    pub(crate) fn remove_by_listener(&mut self, listener: usize) -> Option<Vec<A>> {
        let addrs = self.listener_to_addrs.remove(listener)?;
        for addr in addrs.iter() {
            assert_eq!(self.addr_to_listener.remove(addr), Some(listener));
        }
        Some(addrs)
    }
}

impl<A: Eq + Hash> Default for ListenerSocketMap<A> {
    fn default() -> ListenerSocketMap<A> {
        ListenerSocketMap {
            listener_to_addrs: IdMap::default(),
            addr_to_listener: HashMap::default(),
        }
    }
}

/// A bidirectional map between connection sockets and addresses.
///
/// A `ConnSocketMap` keeps addresses mapped by integer indexes, and allows for
/// constant-time mapping in either direction (though address -> index mappings
/// are via a hash map, and thus slower).
///
/// It differs from a `ListenerSocketMap` in that only a single address per
/// connection is supported.
pub(crate) struct ConnSocketMap<A, S> {
    id_to_sock: IdMap<ConnSocketEntry<S, A>>,
    addr_to_id: HashMap<A, usize>,
}

/// An entry in a [`ConnSocketMap`].
#[derive(Debug, Eq, PartialEq)]
pub(crate) struct ConnSocketEntry<S, A> {
    pub(crate) sock: S,
    pub(crate) addr: A,
}

impl<A: Eq + Hash + Clone, S> ConnSocketMap<A, S> {
    pub(crate) fn insert(&mut self, addr: A, sock: S) -> usize {
        let id = self.id_to_sock.push(ConnSocketEntry { sock, addr: addr.clone() });
        assert_eq!(self.addr_to_id.insert(addr, id), None);
        id
    }
}

impl<A: Eq + Hash, S> ConnSocketMap<A, S> {
    pub(crate) fn get_id_by_addr(&self, addr: &A) -> Option<usize> {
        self.addr_to_id.get(addr).cloned()
    }

    pub(crate) fn get_sock_by_id(&self, id: usize) -> Option<&ConnSocketEntry<S, A>> {
        self.id_to_sock.get(id)
    }

    pub(crate) fn iter_addrs(&self) -> impl Iterator<Item = &'_ A> + ExactSizeIterator {
        self.addr_to_id.keys()
    }

    pub(crate) fn remove_by_id(&mut self, id: usize) -> Option<ConnSocketEntry<S, A>> {
        let ConnSocketEntry { sock, addr } = self.id_to_sock.remove(id)?;
        assert_eq!(self.addr_to_id.remove(&addr), Some(id));
        Some(ConnSocketEntry { sock, addr })
    }

    /// Update the elements of the map in-place, retaining only the elements for
    /// which `f` returns `Ok`.
    ///
    /// `update_retain` has the same behavior as [`IdMap::update_retain`]; see
    /// its documentation for details.
    ///
    /// [`IdMap::update_retain`]: crate::data_structures::IdMap::update_retain
    pub(crate) fn update_retain<'a, E: 'a, F: 'a + Fn(&mut S, &A) -> Result<(), E>>(
        &'a mut self,
        f: F,
    ) -> impl 'a + Iterator<Item = (usize, ConnSocketEntry<S, A>, E)> {
        let Self { id_to_sock, addr_to_id } = self;
        id_to_sock.update_retain(move |ConnSocketEntry { sock, addr }| f(sock, &*addr)).map(
            move |(id, entry, err)| {
                assert_eq!(addr_to_id.remove(&entry.addr), Some(id));
                (id, entry, err)
            },
        )
    }
}

impl<A: Eq + Hash, S> Default for ConnSocketMap<A, S> {
    fn default() -> ConnSocketMap<A, S> {
        ConnSocketMap { id_to_sock: IdMap::default(), addr_to_id: HashMap::default() }
    }
}
