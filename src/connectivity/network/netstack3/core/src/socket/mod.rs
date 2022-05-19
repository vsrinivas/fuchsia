// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! General-purpose socket utilities common to device layer and IP layer
//! sockets.

pub(crate) mod posix;

use alloc::{collections::HashMap, vec::Vec};
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
    type ListenerAddr: Clone + Debug + Hash + Eq;
    /// The type of a connected socket address.
    type ConnAddr: Clone + Debug + Hash + Eq;

    /// The tag value of a socket address vector entry.
    type AddrVecTag: Eq + Copy + Debug + 'static;

    /// An identifier for a listening socket.
    type ListenerId: Clone + Into<usize> + From<usize> + Debug;
    /// An identifier for a connected socket.
    type ConnId: Clone + Into<usize> + From<usize> + Debug;

    /// The state stored for a listening socket.
    type ListenerState;

    /// The state stored for a connected socket.
    type ConnState;

    /// The state stored for a listener socket address.
    type ListenerAddrState: SocketMapAddrStateSpec<
        Self::ListenerAddr,
        Self::ListenerState,
        Self::ListenerId,
        Self,
    >;

    /// The state stored for a connected socket address.
    type ConnAddrState: SocketMapAddrStateSpec<Self::ConnAddr, Self::ConnState, Self::ConnId, Self>;
}

pub(crate) trait SocketMapAddrStateSpec<Addr, State, Id, S: SocketMapSpec + ?Sized> {
    /// Checks whether a new socket with the provided state can be inserted at
    /// the given address in the existing socket map, returning an error
    /// otherwise.
    ///
    /// Implementations of this function should check for any potential
    /// conflicts that would arise when inserting a socket with state
    /// `new_state` into a new or existing entry at `addr` in `socketmap`.
    fn check_for_conflicts(
        new_state: &State,
        addr: &Addr,
        socketmap: &SocketMap<AddrVec<S>, Bound<S>>,
    ) -> Result<(), InsertError>
    where
        Bound<S>: Tagged<AddrVec<S>>;

    /// Gets the target in the existing socket(s) in `self` for a new socket
    /// with the provided state.
    ///
    /// If the new state is incompatible with the existing socket(s),
    /// implementations of this function should return `Err(())`.  If `Ok(dest)`
    /// is returned, the new socket ID will be appended to `dest`.
    fn try_get_dest<'a, 'b>(&'b mut self, new_state: &'a State) -> Result<&'b mut Vec<Id>, ()>;

    /// Creates a new `Self` holding the provided socket with the given new
    /// state at the specified address.
    fn new_addr_state(new_state: &State, id: Id) -> Self;

    /// Removes the given socket from the existing state.
    ///
    /// Implementations should assume that `id` is contained in `remove_from`.
    fn remove_by_id(&mut self, id: Id) -> RemoveResult;
}

#[derive(Derivative)]
#[derivative(Debug(bound = "S::ListenerAddrState: Debug, S::ConnAddrState: Debug"))]
pub(crate) enum Bound<S: SocketMapSpec + ?Sized> {
    Listen(S::ListenerAddrState),
    Conn(S::ConnAddrState),
}

#[derive(Derivative)]
#[derivative(
    Debug(bound = "S::ListenerAddr: Debug, S::ConnAddr: Debug"),
    Clone(bound = "S::ListenerAddr: Clone, S::ConnAddr: Clone"),
    Eq(bound = "S::ListenerAddr: Eq, S::ConnAddr: Eq"),
    PartialEq(bound = "S::ListenerAddr: PartialEq, S::ConnAddr: PartialEq"),
    Hash(bound = "S::ListenerAddr: Hash, S::ConnAddr: Hash")
)]
pub(crate) enum AddrVec<S: SocketMapSpec + ?Sized> {
    Listen(S::ListenerAddr),
    Conn(S::ConnAddr),
}

impl<S: SocketMapSpec> Tagged<AddrVec<S>> for Bound<S>
where
    S::ListenerAddrState: Tagged<S::ListenerAddr, Tag = S::AddrVecTag>,
    S::ConnAddrState: Tagged<S::ConnAddr, Tag = S::AddrVecTag>,
{
    type Tag = S::AddrVecTag;

    fn tag(&self, address: &AddrVec<S>) -> Self::Tag {
        match (self, address) {
            (Bound::Listen(l), AddrVec::Listen(addr)) => l.tag(addr),
            (Bound::Conn(c), AddrVec::Conn(addr)) => c.tag(addr),
            (Bound::Listen(_), AddrVec::Conn(_)) => {
                unreachable!("found listen state for conn addr")
            }
            (Bound::Conn(_), AddrVec::Listen(_)) => {
                unreachable!("found conn state for listen addr")
            }
        }
    }
}

/// The result of attempting to remove a socket from a collection of sockets.
pub(crate) enum RemoveResult {
    /// The value was removed successfully.
    ///
    /// TODO(https://fxbug.dev/97822): remove #[cfg(test)].
    #[cfg(test)]
    Success,
    /// The value is the last value in the collection so the entire collection
    /// should be removed.
    IsLast,
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
pub(crate) struct BoundSocketMap<S: SocketMapSpec>
where
    Bound<S>: Tagged<AddrVec<S>>,
{
    listener_id_to_sock: IdMap<(S::ListenerState, S::ListenerAddr)>,
    conn_id_to_sock: IdMap<(S::ConnState, S::ConnAddr)>,
    addr_to_state: SocketMap<AddrVec<S>, Bound<S>>,
}

impl<S: SocketMapSpec> BoundSocketMap<S>
where
    Bound<S>: Tagged<AddrVec<S>>,
    AddrVec<S>: IterShadows,
{
    pub(crate) fn get_conn_by_addr(&self, addr: &S::ConnAddr) -> Option<&S::ConnAddrState> {
        let Self { listener_id_to_sock: _, conn_id_to_sock: _, addr_to_state } = self;
        addr_to_state.get(&AddrVec::Conn(addr.clone())).map(|state| match state {
            Bound::Conn(conn) => conn,
            Bound::Listen(_) => unreachable!("conn addr does not store conn state"),
        })
    }

    pub(crate) fn get_listener_by_addr(
        &self,
        addr: &S::ListenerAddr,
    ) -> Option<&S::ListenerAddrState> {
        let Self { listener_id_to_sock: _, conn_id_to_sock: _, addr_to_state } = self;
        addr_to_state.get(&AddrVec::Listen(addr.clone())).map(|state| match state {
            Bound::Listen(listen) => listen,
            Bound::Conn(_) => unreachable!("listener addr does not store listener state"),
        })
    }

    pub(crate) fn get_conn_by_id(&self, id: &S::ConnId) -> Option<&(S::ConnState, S::ConnAddr)> {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_state: _ } = self;
        conn_id_to_sock.get(id.clone().into())
    }

    pub(crate) fn get_listener_by_id(
        &self,
        id: &S::ListenerId,
    ) -> Option<&(S::ListenerState, S::ListenerAddr)> {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_state: _ } = self;
        listener_id_to_sock.get(id.clone().into())
    }

    pub(crate) fn iter_addrs(&self) -> impl Iterator<Item = &AddrVec<S>> {
        let Self { listener_id_to_sock: _, conn_id_to_sock: _, addr_to_state } = self;
        addr_to_state.iter().map(|(a, _v): (_, &Bound<S>)| a)
    }

    pub(crate) fn try_insert_listener(
        &mut self,
        listener_addr: S::ListenerAddr,
        state: S::ListenerState,
    ) -> Result<S::ListenerId, InsertError> {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_state } = self;

        Self::try_insert(
            listener_addr.clone(),
            state,
            addr_to_state,
            listener_id_to_sock,
            Bound::Listen,
            AddrVec::Listen,
            |bound| match bound {
                Bound::Listen(l) => l,
                Bound::Conn(_) => unreachable!("listener addr does not have listener state"),
            },
        )
    }

    pub(crate) fn try_insert_conn(
        &mut self,
        conn_addr: S::ConnAddr,
        state: S::ConnState,
    ) -> Result<S::ConnId, InsertError> {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_state } = self;

        Self::try_insert(
            conn_addr,
            state,
            addr_to_state,
            conn_id_to_sock,
            Bound::Conn,
            AddrVec::Conn,
            |bound| match bound {
                Bound::Conn(c) => c,
                Bound::Listen(_) => unreachable!("conn addr does not have conn state"),
            },
        )
    }

    fn try_insert<St, A, V, I>(
        socket_addr: A,
        state: V,
        addr_to_state: &mut SocketMap<AddrVec<S>, Bound<S>>,
        id_to_sock: &mut IdMap<(V, A)>,
        state_to_bound: impl FnOnce(St) -> Bound<S>,
        addr_to_addr_vec: impl FnOnce(A) -> AddrVec<S>,
        unwrap_bound: impl FnOnce(&mut Bound<S>) -> &mut St,
    ) -> Result<I, InsertError>
    where
        St: SocketMapAddrStateSpec<A, V, I, S>,
        A: Clone,
        I: Clone + From<usize>,
    {
        St::check_for_conflicts(&state, &socket_addr, &addr_to_state)?;

        let addr: AddrVec<S> = addr_to_addr_vec(socket_addr.clone());

        match addr_to_state.entry(addr) {
            Entry::Occupied(mut o) => {
                let id = o
                    .map_mut(|bound| {
                        St::try_get_dest(unwrap_bound(bound), &state).map(|v| {
                            let index = id_to_sock.push((state, socket_addr));
                            v.push(index.into());
                            index
                        })
                    })
                    .map_err(|()| InsertError::Exists)?;
                Ok(id.into())
            }
            Entry::Vacant(v) => {
                let index = id_to_sock.push((state, socket_addr));
                let (state, _addr) = id_to_sock.get(index).unwrap();
                v.insert(state_to_bound(St::new_addr_state(state, index.into())));
                Ok(index.into())
            }
        }
    }

    pub(crate) fn try_update_listener_addr(
        &mut self,
        id: &S::ListenerId,
        new_addr: impl FnOnce(S::ListenerAddr) -> S::ListenerAddr,
    ) -> Result<(), ExistsError> {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_state } = self;
        let (_state, addr) = listener_id_to_sock.get_mut(id.clone().into()).unwrap();

        let new_addr = new_addr(addr.clone());
        Self::try_update_addr(
            AddrVec::Listen(addr.clone()),
            AddrVec::Listen(new_addr.clone()),
            addr_to_state,
        )?;
        *addr = new_addr;

        Ok(())
    }

    pub(crate) fn try_update_conn_addr(
        &mut self,
        id: &S::ConnId,
        new_addr: impl FnOnce(S::ConnAddr) -> S::ConnAddr,
    ) -> Result<(), ExistsError> {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_state } = self;
        let (_state, addr) = conn_id_to_sock.get_mut(id.clone().into()).unwrap();

        let new_addr = new_addr(addr.clone());
        Self::try_update_addr(
            AddrVec::Conn(addr.clone()),
            AddrVec::Conn(new_addr.clone()),
            addr_to_state,
        )?;
        *addr = new_addr;

        Ok(())
    }

    fn try_update_addr(
        addr: AddrVec<S>,
        new_addr: AddrVec<S>,
        addr_to_state: &mut SocketMap<AddrVec<S>, Bound<S>>,
    ) -> Result<(), ExistsError> {
        let state = addr_to_state.remove(&addr).expect("existing entry not found");
        let result = match addr_to_state.entry(new_addr) {
            Entry::Occupied(_) => Err(state),
            Entry::Vacant(v) => {
                if v.descendant_counts().len() != 0 {
                    Err(state)
                } else {
                    v.insert(state);
                    Ok(())
                }
            }
        };
        match result {
            Ok(result) => Ok(result),
            Err(to_restore) => {
                // Restore the old state before returning an error.
                match addr_to_state.entry(addr) {
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
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_state } = self;
        Self::remove_by_id(addr_to_state, id, listener_id_to_sock, AddrVec::Listen, |value, id| {
            S::ListenerAddrState::remove_by_id(
                match value {
                    Bound::Listen(l) => l,
                    Bound::Conn(_) => unreachable!("listener state is inconsistent"),
                },
                id,
            )
        })
    }

    pub(crate) fn remove_conn_by_id(
        &mut self,
        id: S::ConnId,
    ) -> Option<(S::ConnState, S::ConnAddr)> {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_state } = self;
        Self::remove_by_id(addr_to_state, id, conn_id_to_sock, AddrVec::Conn, |value, id| {
            S::ConnAddrState::remove_by_id(
                match value {
                    Bound::Conn(c) => c,
                    Bound::Listen(_) => unreachable!("connected state is inconsistent"),
                },
                id,
            )
        })
    }

    fn remove_by_id<I: Into<usize> + Clone, T, A: Clone>(
        addr_to_state: &mut SocketMap<AddrVec<S>, Bound<S>>,
        id: I,
        id_to_sock: &mut IdMap<(T, A)>,
        addr_to_addr_vec: impl FnOnce(A) -> AddrVec<S>,
        remove_from_state: impl FnOnce(&mut Bound<S>, I) -> RemoveResult,
    ) -> Option<(T, A)> {
        id_to_sock.remove(id.clone().into()).map(move |(state, addr)| {
            let mut entry = match addr_to_state.entry(addr_to_addr_vec(addr.clone())) {
                Entry::Vacant(_) => unreachable!("state is inconsistent"),
                Entry::Occupied(o) => o,
            };
            match entry.map_mut(|value| remove_from_state(value, id)) {
                // TODO(https://fxbug.dev/97822): remove #[cfg(test)].
                #[cfg(test)]
                RemoveResult::Success => (),
                RemoveResult::IsLast => {
                    let _: Bound<S> = entry.remove();
                }
            }
            (state, addr)
        })
    }

    pub(crate) fn get_shadower_counts(&self, addr: &AddrVec<S>) -> usize {
        let Self { listener_id_to_sock: _, conn_id_to_sock: _, addr_to_state } = self;
        addr_to_state.descendant_counts(&addr).map(|(_tag, size)| size.get()).sum()
    }
}

#[derive(Debug, Eq, PartialEq)]
pub(crate) enum InsertError {
    ShadowAddrExists,
    Exists,
    ShadowerExists,
    IndirectConflict,
}

#[cfg(test)]
mod tests {
    use alloc::{collections::HashSet, vec, vec::Vec};

    use crate::testutil::set_logger_for_test;

    use super::*;

    enum FakeSpec {}

    type ListenerAddr = (u16, Option<&'static str>);
    type ConnAddr = (u16, &'static str, &'static str);

    impl From<ListenerAddr> for AddrVec<FakeSpec> {
        fn from((port, name): (u16, Option<&'static str>)) -> Self {
            Self::Listen((port, name))
        }
    }

    impl From<ConnAddr> for AddrVec<FakeSpec> {
        fn from((port, name, remote): (u16, &'static str, &'static str)) -> Self {
            Self::Conn((port, name, remote))
        }
    }

    impl IterShadows for AddrVec<FakeSpec> {
        type IterShadows = <Vec<Self> as IntoIterator>::IntoIter;

        fn iter_shadows(&self) -> Self::IterShadows {
            match *self {
                AddrVec::Listen((_port, None)) => vec![],
                AddrVec::Listen((port, Some(_))) => {
                    vec![AddrVec::Listen((port, None))]
                }
                AddrVec::Conn((port, name, _remote)) => {
                    vec![AddrVec::Listen((port, Some(name))), AddrVec::Listen((port, None))]
                }
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

    #[derive(PartialEq, Eq, Debug)]
    struct Multiple<T>(char, Vec<T>);

    impl<T, A> Tagged<A> for Multiple<T> {
        type Tag = char;
        fn tag(&self, _: &A) -> Self::Tag {
            let Multiple(c, _) = self;
            *c
        }
    }

    #[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
    struct Conn(usize);

    impl SocketMapSpec for FakeSpec {
        type ListenerAddr = (u16, Option<&'static str>);
        type ConnAddr = (u16, &'static str, &'static str);
        type AddrVecTag = char;

        type ListenerId = Listener;
        type ConnId = Conn;

        type ListenerState = char;
        type ConnState = char;

        type ListenerAddrState = Multiple<Listener>;
        type ConnAddrState = Multiple<Conn>;
    }

    impl<A: Into<AddrVec<FakeSpec>> + Clone, I: Eq> SocketMapAddrStateSpec<A, char, I, FakeSpec>
        for Multiple<I>
    {
        fn check_for_conflicts(
            new_state: &char,
            addr: &A,
            socketmap: &SocketMap<AddrVec<FakeSpec>, Bound<FakeSpec>>,
        ) -> Result<(), InsertError> {
            let dest = addr.clone().into();
            if dest.iter_shadows().any(|a| socketmap.get(&a).is_some()) {
                return Err(InsertError::ShadowAddrExists);
            }
            match socketmap.get(&dest) {
                Some(Bound::Listen(Multiple(c, _))) | Some(Bound::Conn(Multiple(c, _))) => {
                    if c != new_state {
                        return Err(InsertError::Exists);
                    }
                }
                None => (),
            }
            if socketmap.descendant_counts(&dest).len() != 0 {
                Err(InsertError::ShadowerExists)
            } else {
                Ok(())
            }
        }

        fn try_get_dest<'a, 'b>(&'b mut self, new_state: &'a char) -> Result<&'b mut Vec<I>, ()> {
            let Self(c, v) = self;
            (new_state == c).then(|| v).ok_or(())
        }

        fn new_addr_state(new_state: &char, id: I) -> Self {
            Self(*new_state, vec![id])
        }

        fn remove_by_id(&mut self, id: I) -> RemoveResult {
            let Self(_, v) = self;
            let index = v.iter().position(|i| i == &id).expect("did not find id");
            let _: I = v.swap_remove(index);
            if v.is_empty() {
                RemoveResult::IsLast
            } else {
                RemoveResult::Success
            }
        }
    }

    #[test]
    fn bound_insert_get_remove_listener() {
        set_logger_for_test();
        let mut bound = BoundSocketMap::<FakeSpec>::default();

        let addr = (1, Some("aaa"));

        let id = bound.try_insert_listener(addr, 'v').unwrap();
        assert_eq!(bound.get_listener_by_id(&id), Some(&('v', addr)));
        assert_eq!(bound.get_listener_by_addr(&addr), Some(&Multiple('v', vec![id])));

        assert_eq!(bound.remove_listener_by_id(id), Some(('v', addr)));
        assert_eq!(bound.get_listener_by_addr(&addr), None);
        assert_eq!(bound.get_listener_by_id(&id), None);
    }

    #[test]
    fn bound_insert_get_remove_conn() {
        set_logger_for_test();
        let mut bound = BoundSocketMap::<FakeSpec>::default();

        let addr = (1, "aaa", "remote");

        let id = bound.try_insert_conn(addr, 'v').unwrap();
        assert_eq!(bound.get_conn_by_id(&id), Some(&('v', addr)));
        assert_eq!(bound.get_conn_by_addr(&addr), Some(&Multiple('v', vec![id])));

        assert_eq!(bound.remove_conn_by_id(id), Some(('v', addr)));
        assert_eq!(bound.get_conn_by_addr(&addr), None);
        assert_eq!(bound.get_conn_by_id(&id), None);
    }

    #[test]
    fn bound_iter_addrs() {
        set_logger_for_test();
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
        set_logger_for_test();
        let mut bound = BoundSocketMap::<FakeSpec>::default();
        let addr = (1, None);

        let _id = bound.try_insert_listener(addr, 'a').unwrap();
        assert_eq!(bound.try_insert_listener(addr, 'b'), Err(InsertError::Exists));
    }

    #[test]
    fn insert_listener_conflict_with_shadower() {
        set_logger_for_test();
        let mut bound = BoundSocketMap::<FakeSpec>::default();
        let addr = (1, None);
        let shadows_addr = (1, Some("abc"));

        let _id = bound.try_insert_listener(addr, 'a').unwrap();
        assert_eq!(
            bound.try_insert_listener(shadows_addr, 'b'),
            Err(InsertError::ShadowAddrExists)
        );
    }

    #[test]
    fn insert_conn_conflict_with_listener() {
        set_logger_for_test();
        let mut bound = BoundSocketMap::<FakeSpec>::default();
        let addr = (1, None);
        let shadows_addr = (1, "abc", "remote");

        let _id = bound.try_insert_listener(addr, 'a').unwrap();
        assert_eq!(bound.try_insert_conn(shadows_addr, 'b'), Err(InsertError::ShadowAddrExists));
    }

    #[test]
    fn insert_and_remove_listener() {
        set_logger_for_test();
        let mut bound = BoundSocketMap::<FakeSpec>::default();
        let addr = (1, None);

        let a = bound.try_insert_listener(addr, 'x').unwrap();
        let b = bound.try_insert_listener(addr, 'x').unwrap();
        assert_ne!(a, b);

        assert_eq!(bound.remove_listener_by_id(a), Some(('x', addr)));
        assert_eq!(bound.get_listener_by_addr(&addr), Some(&Multiple('x', vec![b])));
    }

    #[test]
    fn insert_and_remove_conn() {
        set_logger_for_test();
        let mut bound = BoundSocketMap::<FakeSpec>::default();
        let addr = (1, "a", "b");

        let a = bound.try_insert_conn(addr, 'x').unwrap();
        let b = bound.try_insert_conn(addr, 'x').unwrap();
        assert_ne!(a, b);

        assert_eq!(bound.remove_conn_by_id(a), Some(('x', addr)));
        assert_eq!(bound.get_conn_by_addr(&addr), Some(&Multiple('x', vec![b])));
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
