// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! General-purpose socket utilities common to device layer and IP layer
//! sockets.

pub(crate) mod posix;

use alloc::collections::HashMap;
use core::fmt::Debug;
use core::hash::Hash;

use derivative::Derivative;

use crate::{
    data_structures::{
        socketmap::{Entry, IterShadows, SocketMap, Tagged},
        IdMap,
    },
    error::ExistsError,
};

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

/// A bidirectional map between connection sockets and addresses.
///
/// A `ConnSocketMap` keeps addresses mapped by integer indexes, and allows for
/// constant-time mapping in either direction (though address -> index mappings
/// are via a hash map, and thus slower).
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

/// Specifies the types parameters for [`BoundSocketMap`] as a single bundle.
pub(crate) trait SocketMapSpec {
    /// The type of a listener socket address.
    type ListenerAddr: Clone + Into<Self::AddrVec> + Debug;
    /// The type of a connected socket address.
    type ConnAddr: Clone + Into<Self::AddrVec> + Debug;
    /// The type of a socket address vector.
    type AddrVec: Hash + Eq + IterShadows + Clone + Debug;

    /// An identifier for a listening socket.
    type ListenerId: Clone + Into<usize> + From<usize> + Debug;
    /// An identifier for a connected socket.
    type ConnId: Clone + Into<usize> + From<usize> + Debug;

    /// The state stored for a listening socket.
    type ListenerState;
    /// The state stored for a connected socket.
    type ConnState;
}

enum Bound<S: SocketMapSpec> {
    Listen(S::ListenerId),
    Conn(S::ConnId),
}

/// A bidirectional map between sockets and their state, keyed in one direction
/// by socket IDs, and in the other by socket addresses.
///
/// The types of keys and IDs is determined by the [`SocketMapSpec`] parameter.
/// Each listener and connected socket stores additional state. Listener and
/// connected sockets are keyed independently, but share the same address vector
/// space. Conflicts are detected on attempted insertion of new sockets.
#[derive(Derivative)]
#[derivative(Default(bound = ""))]
pub(crate) struct BoundSocketMap<S: SocketMapSpec> {
    listener_id_to_sock: IdMap<(S::ListenerState, S::ListenerAddr)>,
    conn_id_to_sock: IdMap<(S::ConnState, S::ConnAddr)>,
    addr_to_id: SocketMap<S::AddrVec, Bound<S>>,
}

impl<S: SocketMapSpec> Tagged for Bound<S> {
    // This is currently a unit struct since we don't allow sharing or
    // shadowing.  This will need to become more complicated to support
    // SO_REUSEADDR and SO_REUSEPORT socket options.
    type Tag = ();

    fn tag(&self) -> Self::Tag {}
}

impl<S: SocketMapSpec> BoundSocketMap<S> {
    pub(crate) fn get_conn_by_addr(&self, addr: &S::ConnAddr) -> Option<&S::ConnId> {
        let Self { listener_id_to_sock: _, conn_id_to_sock: _, addr_to_id } = self;
        addr_to_id.get(&addr.clone().into()).map(|id| match id {
            Bound::Conn(id) => id,
            Bound::Listen(id) => unreachable!("conn addr stores listener ID {:?}", id),
        })
    }

    pub(crate) fn get_listener_by_addr(&self, addr: &S::ListenerAddr) -> Option<&S::ListenerId> {
        let Self { listener_id_to_sock: _, conn_id_to_sock: _, addr_to_id } = self;
        addr_to_id.get(&addr.clone().into()).map(|id| match id {
            Bound::Conn(id) => unreachable!("listener addr stores conn ID {:?}", id),
            Bound::Listen(id) => id,
        })
    }

    pub(crate) fn get_conn_by_id(&self, id: &S::ConnId) -> Option<&(S::ConnState, S::ConnAddr)> {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_id: _ } = self;
        conn_id_to_sock.get(id.clone().into())
    }

    pub(crate) fn get_listener_by_id(
        &self,
        id: &S::ListenerId,
    ) -> Option<&(S::ListenerState, S::ListenerAddr)> {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_id: _ } = self;
        listener_id_to_sock.get(id.clone().into())
    }

    pub(crate) fn iter_addrs(&self) -> impl Iterator<Item = &S::AddrVec> {
        let Self { listener_id_to_sock: _, conn_id_to_sock: _, addr_to_id } = self;
        addr_to_id.iter().map(|(a, _v): (_, &Bound<S>)| a)
    }

    pub(crate) fn try_insert_listener(
        &mut self,
        listener_addr: S::ListenerAddr,
        state: S::ListenerState,
    ) -> Result<S::ListenerId, InsertListenerError> {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_id } = self;

        Self::try_insert(listener_addr, state, addr_to_id, listener_id_to_sock, Bound::Listen)
            .map_err(|e| match e {
                InsertError::ShadowAddrExists => InsertListenerError::ShadowAddrExists,
                InsertError::Exists => InsertListenerError::ListenerExists,
                InsertError::ShadowerExists => InsertListenerError::ShadowerExists,
            })
    }

    pub(crate) fn try_insert_conn(
        &mut self,
        conn_addr: S::ConnAddr,
        state: S::ConnState,
    ) -> Result<S::ConnId, InsertConnError> {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_id } = self;

        Self::try_insert(conn_addr, state, addr_to_id, conn_id_to_sock, Bound::Conn).map_err(|e| {
            match e {
                InsertError::ShadowAddrExists => InsertConnError::ShadowAddrExists,
                InsertError::Exists => InsertConnError::ConnExists,
                InsertError::ShadowerExists => InsertConnError::ShadowerExists,
            }
        })
    }

    fn try_insert<A: Into<S::AddrVec> + Clone, V, I: Clone + From<usize>>(
        socket_addr: A,
        state: V,
        addr_to_id: &mut SocketMap<S::AddrVec, Bound<S>>,
        id_to_sock: &mut IdMap<(V, A)>,
        to_bound: fn(I) -> Bound<S>,
    ) -> Result<I, InsertError> {
        let addr: S::AddrVec = socket_addr.clone().into();
        for s in addr.iter_shadows() {
            if let Some(_id) = addr_to_id.get(&s) {
                return Err(InsertError::ShadowAddrExists);
            }
        }

        if addr_to_id.descendant_counts(&addr).len() != 0 {
            return Err(InsertError::ShadowerExists);
        }

        match addr_to_id.entry(addr) {
            Entry::Occupied(_o) => Err(InsertError::Exists),
            Entry::Vacant(v) => {
                let index = id_to_sock.push((state, socket_addr));
                let id: I = index.into();
                v.insert(to_bound(id.clone()));
                Ok(id)
            }
        }
    }

    pub(crate) fn try_update_listener_addr(
        &mut self,
        id: &S::ListenerId,
        new_addr: impl FnOnce(S::ListenerAddr) -> S::ListenerAddr,
    ) -> Result<(), ExistsError> {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_id } = self;
        let (_state, addr) = listener_id_to_sock.get_mut(id.clone().into()).unwrap();

        let new_addr = Self::try_update_addr(addr.clone(), new_addr(addr.clone()), addr_to_id)?;
        *addr = new_addr;

        Ok(())
    }

    pub(crate) fn try_update_conn_addr(
        &mut self,
        id: &S::ConnId,
        new_addr: impl FnOnce(S::ConnAddr) -> S::ConnAddr,
    ) -> Result<(), ExistsError> {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_id } = self;
        let (_state, addr) = conn_id_to_sock.get_mut(id.clone().into()).unwrap();

        let new_addr = Self::try_update_addr(addr.clone(), new_addr(addr.clone()), addr_to_id)?;
        *addr = new_addr;

        Ok(())
    }

    fn try_update_addr<A: Into<S::AddrVec> + Clone, B: Into<S::AddrVec> + Clone>(
        addr: A,
        new_addr: B,
        addr_to_id: &mut SocketMap<S::AddrVec, Bound<S>>,
    ) -> Result<B, ExistsError> {
        let addr = addr.into();
        let state = addr_to_id.remove(&addr).expect("existing entry not found");
        let result = match addr_to_id.entry(new_addr.clone().into()) {
            Entry::Occupied(_) => Err(state),
            Entry::Vacant(v) => {
                if v.descendant_counts().len() != 0 {
                    Err(state)
                } else {
                    v.insert(state);
                    Ok(new_addr)
                }
            }
        };
        match result {
            Ok(result) => Ok(result),
            Err(to_restore) => {
                // Restore the old state before returning an error.
                match addr_to_id.entry(addr) {
                    Entry::Occupied(_) => unreachable!("just-removed-from entry is occupied"),
                    Entry::Vacant(v) => v.insert(to_restore),
                };
                Err(ExistsError)
            }
        }
    }

    pub(crate) fn remove_listener_by_id(
        &mut self,
        id: S::ListenerId,
    ) -> Option<(S::ListenerState, S::ListenerAddr)> {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_id } = self;
        listener_id_to_sock.remove(id.into()).map(|(state, addr)| {
            let _: Bound<_> =
                addr_to_id.remove(&addr.clone().into()).expect("listener state is inconsistent");
            (state, addr)
        })
    }

    pub(crate) fn remove_conn_by_id(
        &mut self,
        id: S::ConnId,
    ) -> Option<(S::ConnState, S::ConnAddr)> {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_id } = self;
        conn_id_to_sock.remove(id.into()).map(|(state, addr)| {
            let _: Bound<_> =
                addr_to_id.remove(&addr.clone().into()).expect("connected state is inconsistent");
            (state, addr)
        })
    }

    pub(crate) fn get_shadower_counts(&self, addr: &S::AddrVec) -> usize {
        let Self { listener_id_to_sock: _, conn_id_to_sock: _, addr_to_id } = self;
        addr_to_id.descendant_counts(&addr).map(|(_tag, size)| size.get()).sum()
    }
}

enum InsertError {
    ShadowAddrExists,
    Exists,
    ShadowerExists,
}

#[derive(Debug, Eq, PartialEq)]
pub(crate) enum InsertListenerError {
    ShadowAddrExists,
    ListenerExists,
    ShadowerExists,
}

#[derive(Debug, Eq, PartialEq)]
pub(crate) enum InsertConnError {
    ShadowAddrExists,
    ConnExists,
    ShadowerExists,
}

#[cfg(test)]
mod tests {
    use alloc::{collections::HashSet, vec, vec::Vec};

    use super::*;

    enum FakeSpec {}

    type ListenerAddr = (u16, Option<&'static str>);
    type ConnAddr = (u16, &'static str, &'static str);

    #[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
    enum AddrVec {
        Listen { port: u16, name: Option<&'static str> },
        Conn { port: u16, name: &'static str, remote: &'static str },
    }

    impl From<ListenerAddr> for AddrVec {
        fn from((port, name): (u16, Option<&'static str>)) -> Self {
            Self::Listen { port, name }
        }
    }

    impl From<ConnAddr> for AddrVec {
        fn from((port, name, remote): (u16, &'static str, &'static str)) -> Self {
            Self::Conn { port, name, remote }
        }
    }

    impl IterShadows for AddrVec {
        type IterShadows = <Vec<Self> as IntoIterator>::IntoIter;

        fn iter_shadows(&self) -> Self::IterShadows {
            match *self {
                AddrVec::Listen { port: _, name: None } => vec![],
                AddrVec::Listen { port, name: Some(_) } => {
                    vec![AddrVec::Listen { port, name: None }]
                }
                AddrVec::Conn { port, name, remote: _ } => vec![
                    AddrVec::Listen { port, name: Some(name) },
                    AddrVec::Listen { port, name: None },
                ],
            }
            .into_iter()
        }
    }

    #[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
    struct Listener(usize);

    impl From<Listener> for usize {
        fn from(Listener(index): Listener) -> Self {
            index
        }
    }

    impl From<usize> for Listener {
        fn from(index: usize) -> Listener {
            Listener(index)
        }
    }

    impl From<Conn> for usize {
        fn from(Conn(index): Conn) -> Self {
            index
        }
    }

    impl From<usize> for Conn {
        fn from(index: usize) -> Conn {
            Conn(index)
        }
    }

    #[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
    struct Conn(usize);

    impl SocketMapSpec for FakeSpec {
        type ListenerAddr = (u16, Option<&'static str>);
        type ConnAddr = (u16, &'static str, &'static str);
        type AddrVec = AddrVec;

        type ListenerId = Listener;
        type ConnId = Conn;

        type ListenerState = char;
        type ConnState = char;
    }

    #[test]
    fn bound_insert_get_remove_listener() {
        let mut bound = BoundSocketMap::<FakeSpec>::default();

        let addr = (1, Some("aaa"));

        let id = bound.try_insert_listener(addr, 'v').unwrap();
        assert_eq!(bound.get_listener_by_id(&id), Some(&('v', addr)));
        assert_eq!(bound.get_listener_by_addr(&addr), Some(&id));

        assert_eq!(bound.remove_listener_by_id(id), Some(('v', addr)));
        assert_eq!(bound.get_listener_by_addr(&addr), None);
        assert_eq!(bound.get_listener_by_id(&id), None);
    }

    #[test]
    fn bound_insert_get_remove_conn() {
        let mut bound = BoundSocketMap::<FakeSpec>::default();

        let addr = (1, "aaa", "remote");

        let id = bound.try_insert_conn(addr, 'v').unwrap();
        assert_eq!(bound.get_conn_by_id(&id), Some(&('v', addr)));
        assert_eq!(bound.get_conn_by_addr(&addr), Some(&id));

        assert_eq!(bound.remove_conn_by_id(id), Some(('v', addr)));
        assert_eq!(bound.get_conn_by_addr(&addr), None);
        assert_eq!(bound.get_conn_by_id(&id), None);
    }

    #[test]
    fn bound_iter_addrs() {
        let mut bound = BoundSocketMap::<FakeSpec>::default();

        let listener_addrs = [(1, Some("aaa")), (2, Some("aaa")), (3, Some("aaa")), (4, None)];
        let conn_addrs = [(1, "bbb", "xxx"), (2, "bbb", "xxx"), (3, "bbb", "yyy")];

        for (port, local) in listener_addrs.iter().cloned() {
            let value = char::from_u32(u32::from(port)).unwrap();
            let _: Listener = bound.try_insert_listener((port, local), value).unwrap();
        }
        for (port, local, remote) in conn_addrs.iter().cloned() {
            let value = char::from_u32(u32::from(port)).unwrap();
            let _: Conn = bound.try_insert_conn((port, local, remote), value).unwrap();
        }
        let expected_addrs = IntoIterator::into_iter(listener_addrs)
            .map(Into::into)
            .chain(IntoIterator::into_iter(conn_addrs).map(Into::into))
            .collect::<HashSet<_>>();

        assert_eq!(expected_addrs, bound.iter_addrs().cloned().collect());
    }

    #[test]
    fn insert_listener_conflict_with_listener() {
        let mut bound = BoundSocketMap::<FakeSpec>::default();
        let addr = (1, None);

        let _id = bound.try_insert_listener(addr, 'a').unwrap();
        assert_eq!(bound.try_insert_listener(addr, 'b'), Err(InsertListenerError::ListenerExists));
    }

    #[test]
    fn insert_listener_conflict_with_shadower() {
        let mut bound = BoundSocketMap::<FakeSpec>::default();
        let addr = (1, None);
        let shadows_addr = (1, Some("abc"));

        let _id = bound.try_insert_listener(addr, 'a').unwrap();
        assert_eq!(
            bound.try_insert_listener(shadows_addr, 'b'),
            Err(InsertListenerError::ShadowAddrExists)
        );
    }

    #[test]
    fn insert_conn_conflict_with_listener() {
        let mut bound = BoundSocketMap::<FakeSpec>::default();
        let addr = (1, None);
        let shadows_addr = (1, "abc", "remote");

        let _id = bound.try_insert_listener(addr, 'a').unwrap();
        assert_eq!(
            bound.try_insert_conn(shadows_addr, 'b'),
            Err(InsertConnError::ShadowAddrExists)
        );
    }

    #[test]
    fn update_listener_to_shadowed_addr_fails() {
        let mut bound = BoundSocketMap::<FakeSpec>::default();
        const FIRST: (u16, Option<&'static str>) = (1, Some("aaa"));
        const SECOND: (u16, Option<&'static str>) = (1, Some("yyy"));

        let first = bound.try_insert_listener(FIRST, 'a').unwrap();
        let second = bound.try_insert_listener(SECOND, 'b').unwrap();

        // Moving from (1, "aaa") to (1, None) should fail since it is shadowed
        // by (1, "yyy"), and vise versa.
        assert_eq!(bound.try_update_listener_addr(&second, |(a, _b)| (a, None)), Err(ExistsError));
        assert_eq!(bound.try_update_listener_addr(&first, |(a, _b)| (a, None)), Err(ExistsError));
    }
}
