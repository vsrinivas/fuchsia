// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! General-purpose socket utilities common to device layer and IP layer
//! sockets.

pub(crate) mod address;
pub(crate) mod posix;

use alloc::{collections::HashMap, vec::Vec};
use core::{fmt::Debug, hash::Hash};
use net_types::ip::IpAddress;

use derivative::Derivative;

use crate::{
    data_structures::{
        socketmap::{Entry, IterShadows, SocketMap, Tagged},
        IdMap,
    },
    error::ExistsError,
    ip::IpDeviceId,
    socket::address::{ConnAddr, ListenerAddr},
};

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
}

impl<A: Eq + Hash, S> Default for ConnSocketMap<A, S> {
    fn default() -> ConnSocketMap<A, S> {
        ConnSocketMap { id_to_sock: IdMap::default(), addr_to_id: HashMap::default() }
    }
}

pub(crate) trait SocketMapAddrSpec {
    /// The type of IP addresses in the socket address.
    type IpAddr: IpAddress;
    /// The type of the device component of a socket address.
    type DeviceId: IpDeviceId;
    /// The local identifier portion of a socket address.
    type LocalIdentifier: Clone + Debug + Hash + Eq;
    /// The remote identifier portion of a socket address.
    type RemoteIdentifier: Clone + Debug + Hash + Eq;
}

/// Specifies the types parameters for [`BoundSocketMap`] state as a single bundle.
pub(crate) trait SocketMapStateSpec {
    /// The tag value of a socket address vector entry.
    ///
    /// These values are derived from [`Self::ListenerAddrState`] and
    /// [`Self::ConnAddrState`].
    type AddrVecTag: Eq + Copy + Debug + 'static;

    /// An identifier for a listening socket.
    type ListenerId: Clone + Into<usize> + From<usize> + Debug;
    /// An identifier for a connected socket.
    type ConnId: Clone + Into<usize> + From<usize> + Debug;

    /// The state stored for a listening socket.
    type ListenerState;
    /// The state stored for a listening socket that is used to determine
    /// whether sockets can share an address.
    type ListenerSharingState;

    /// The state stored for a connected socket.
    type ConnState;
    /// The state stored for a connected socket that is used to determine
    /// whether sockets can share an address.
    type ConnSharingState;

    /// The state stored for a listener socket address.
    type ListenerAddrState;

    /// The state stored for a connected socket address.
    type ConnAddrState;
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub(crate) struct IncompatibleError;

pub(crate) trait SocketMapAddrStateSpec<
    Addr,
    SharingState,
    Id,
    A: SocketMapAddrSpec,
    S: SocketMapStateSpec + ?Sized,
>
{
    /// Checks whether a new socket with the provided state can be inserted at
    /// the given address in the existing socket map, returning an error
    /// otherwise.
    ///
    /// Implementations of this function should check for any potential
    /// conflicts that would arise when inserting a socket with state
    /// `new_sharing_state` into a new or existing entry at `addr` in
    /// `socketmap`.
    fn check_for_conflicts(
        new_sharing_state: &SharingState,
        addr: &Addr,
        socketmap: &SocketMap<AddrVec<A>, Bound<S>>,
    ) -> Result<(), InsertError>
    where
        Bound<S>: Tagged<AddrVec<A>>;

    /// Gets the target in the existing socket(s) in `self` for a new socket
    /// with the provided state.
    ///
    /// If the new state is incompatible with the existing socket(s),
    /// implementations of this function should return
    /// `Err(IncompatibleError)`. If `Ok(dest)` is returned, the new socket ID
    /// will be appended to `dest`.
    fn try_get_dest<'a, 'b>(
        &'b mut self,
        new_sharing_state: &'a SharingState,
    ) -> Result<&'b mut Vec<Id>, IncompatibleError>;

    /// Creates a new `Self` holding the provided socket with the given new
    /// sharing state at the specified address.
    fn new_addr_state(new_sharing_state: &SharingState, id: Id) -> Self;

    /// Removes the given socket from the existing state.
    ///
    /// Implementations should assume that `id` is contained in `self`.
    fn remove_by_id(&mut self, id: Id) -> RemoveResult;
}

#[derive(Derivative)]
#[derivative(Debug(bound = "S::ListenerAddrState: Debug, S::ConnAddrState: Debug"))]
pub(crate) enum Bound<S: SocketMapStateSpec + ?Sized> {
    Listen(S::ListenerAddrState),
    Conn(S::ConnAddrState),
}

#[derive(Derivative)]
#[derivative(
    Debug(bound = ""),
    Clone(bound = ""),
    Eq(bound = ""),
    PartialEq(bound = ""),
    Hash(bound = "")
)]
pub(crate) enum AddrVec<A: SocketMapAddrSpec + ?Sized> {
    Listen(ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>),
    Conn(ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>),
}

impl<A: SocketMapAddrSpec, S: SocketMapStateSpec> Tagged<AddrVec<A>> for Bound<S>
where
    S::ListenerAddrState:
        Tagged<ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>, Tag = S::AddrVecTag>,
    S::ConnAddrState: Tagged<
        ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
        Tag = S::AddrVecTag,
    >,
{
    type Tag = S::AddrVecTag;

    fn tag(&self, address: &AddrVec<A>) -> Self::Tag {
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
    Success,
    /// The value is the last value in the collection so the entire collection
    /// should be removed.
    IsLast,
}

/// A bidirectional map between sockets and their state, keyed in one direction
/// by socket IDs, and in the other by socket addresses.
///
/// The types of keys and IDs is determined by the [`SocketMapStateSpec`]
/// parameter. Each listener and connected socket stores additional state.
/// Listener and connected sockets are keyed independently, but share the same
/// address vector space. Conflicts are detected on attempted insertion of new
/// sockets.
#[derive(Derivative)]
#[derivative(Default(bound = ""))]
pub(crate) struct BoundSocketMap<A: SocketMapAddrSpec, S: SocketMapStateSpec>
where
    Bound<S>: Tagged<AddrVec<A>>,
{
    listener_id_to_sock: IdMap<(
        S::ListenerState,
        S::ListenerSharingState,
        ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
    )>,
    conn_id_to_sock: IdMap<(
        S::ConnState,
        S::ConnSharingState,
        ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
    )>,
    addr_to_state: SocketMap<AddrVec<A>, Bound<S>>,
}

impl<A: SocketMapAddrSpec, S: SocketMapStateSpec> BoundSocketMap<A, S>
where
    Bound<S>: Tagged<AddrVec<A>>,
    AddrVec<A>: IterShadows,
    S::ConnAddrState: SocketMapAddrStateSpec<
        ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
        S::ConnSharingState,
        S::ConnId,
        A,
        S,
    >,
    S::ListenerAddrState: SocketMapAddrStateSpec<
        ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
        S::ListenerSharingState,
        S::ListenerId,
        A,
        S,
    >,
{
    pub(crate) fn get_conn_by_addr(
        &self,
        addr: &ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
    ) -> Option<&S::ConnAddrState> {
        let Self { listener_id_to_sock: _, conn_id_to_sock: _, addr_to_state } = self;
        addr_to_state.get(&AddrVec::Conn(addr.clone())).map(|state| match state {
            Bound::Conn(conn) => conn,
            Bound::Listen(_) => unreachable!("conn addr does not store conn state"),
        })
    }

    pub(crate) fn get_listener_by_addr(
        &self,
        addr: &ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
    ) -> Option<&S::ListenerAddrState> {
        let Self { listener_id_to_sock: _, conn_id_to_sock: _, addr_to_state } = self;
        addr_to_state.get(&AddrVec::Listen(addr.clone())).map(|state| match state {
            Bound::Listen(listen) => listen,
            Bound::Conn(_) => unreachable!("listener addr does not store listener state"),
        })
    }

    pub(crate) fn get_conn_by_id(
        &self,
        id: &S::ConnId,
    ) -> Option<&(
        S::ConnState,
        S::ConnSharingState,
        ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
    )> {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_state: _ } = self;
        conn_id_to_sock.get(id.clone().into())
    }

    pub(crate) fn get_listener_by_id(
        &self,
        id: &S::ListenerId,
    ) -> Option<&(
        S::ListenerState,
        S::ListenerSharingState,
        ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
    )> {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_state: _ } = self;
        listener_id_to_sock.get(id.clone().into())
    }

    pub(crate) fn get_listener_by_id_mut(
        &mut self,
        id: &S::ListenerId,
    ) -> Option<(
        &mut S::ListenerState,
        &S::ListenerSharingState,
        &ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
    )> {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_state: _ } = self;
        listener_id_to_sock
            .get_mut(id.clone().into())
            .map(|(state, sharing_state, addr)| (state, &*sharing_state, &*addr))
    }

    pub(crate) fn get_conn_by_id_mut(
        &mut self,
        id: &S::ConnId,
    ) -> Option<(
        &mut S::ConnState,
        &S::ConnSharingState,
        &ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
    )> {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_state: _ } = self;
        conn_id_to_sock
            .get_mut(id.clone().into())
            .map(|(state, sharing_state, addr)| (state, &*sharing_state, &*addr))
    }

    pub(crate) fn iter_addrs(&self) -> impl Iterator<Item = &AddrVec<A>> {
        let Self { listener_id_to_sock: _, conn_id_to_sock: _, addr_to_state } = self;
        addr_to_state.iter().map(|(a, _v): (_, &Bound<S>)| a)
    }

    pub(crate) fn try_insert_listener(
        &mut self,
        listener_addr: ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
        state: S::ListenerState,
        sharing_state: S::ListenerSharingState,
    ) -> Result<S::ListenerId, (InsertError, S::ListenerState, S::ListenerSharingState)> {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_state } = self;

        Self::try_insert(
            listener_addr.clone(),
            state,
            sharing_state,
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
        conn_addr: ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
        state: S::ConnState,
        sharing_state: S::ConnSharingState,
    ) -> Result<S::ConnId, (InsertError, S::ConnState, S::ConnSharingState)> {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_state } = self;

        Self::try_insert(
            conn_addr,
            state,
            sharing_state,
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

    fn try_insert<St, SA, V, T, I>(
        socket_addr: SA,
        state: V,
        sharing_state: T,
        addr_to_state: &mut SocketMap<AddrVec<A>, Bound<S>>,
        id_to_sock: &mut IdMap<(V, T, SA)>,
        state_to_bound: impl FnOnce(St) -> Bound<S>,
        addr_to_addr_vec: impl FnOnce(SA) -> AddrVec<A>,
        unwrap_bound: impl FnOnce(&mut Bound<S>) -> &mut St,
    ) -> Result<I, (InsertError, V, T)>
    where
        St: SocketMapAddrStateSpec<SA, T, I, A, S>,
        SA: Clone,
        I: Clone + From<usize>,
    {
        match St::check_for_conflicts(&sharing_state, &socket_addr, &addr_to_state) {
            Err(e) => return Err((e, state, sharing_state)),
            Ok(()) => (),
        };

        let addr = addr_to_addr_vec(socket_addr.clone());

        match addr_to_state.entry(addr) {
            Entry::Occupied(mut o) => {
                let id = o.map_mut(|bound| {
                    match St::try_get_dest(unwrap_bound(bound), &sharing_state) {
                        Ok(v) => {
                            let index = id_to_sock.push((state, sharing_state, socket_addr));
                            v.push(index.into());
                            Ok(index)
                        }
                        Err(IncompatibleError) => Err((InsertError::Exists, state, sharing_state)),
                    }
                })?;
                Ok(id.into())
            }
            Entry::Vacant(v) => {
                let index = id_to_sock.push((state, sharing_state, socket_addr));
                let (_state, sharing_state, _addr): &(V, _, SA) = id_to_sock.get(index).unwrap();
                v.insert(state_to_bound(St::new_addr_state(sharing_state, index.into())));
                Ok(index.into())
            }
        }
    }

    pub(crate) fn try_update_listener_addr(
        &mut self,
        id: &S::ListenerId,
        new_addr: impl FnOnce(
            ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
        ) -> ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
    ) -> Result<(), ExistsError> {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_state } = self;
        let (_state, _sharing_state, addr) =
            listener_id_to_sock.get_mut(id.clone().into()).unwrap();

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
        new_addr: impl FnOnce(
            ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
        ) -> ConnAddr<
            A::IpAddr,
            A::DeviceId,
            A::LocalIdentifier,
            A::RemoteIdentifier,
        >,
    ) -> Result<(), ExistsError> {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_state } = self;
        let (_state, _sharing_state, addr) = conn_id_to_sock.get_mut(id.clone().into()).unwrap();

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
        addr: AddrVec<A>,
        new_addr: AddrVec<A>,
        addr_to_state: &mut SocketMap<AddrVec<A>, Bound<S>>,
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
    ) -> Option<(
        S::ListenerState,
        S::ListenerSharingState,
        ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
    )> {
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
    ) -> Option<(
        S::ConnState,
        S::ConnSharingState,
        ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
    )> {
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

    fn remove_by_id<I: Into<usize> + Clone, V, T, AA: Clone>(
        addr_to_state: &mut SocketMap<AddrVec<A>, Bound<S>>,
        id: I,
        id_to_sock: &mut IdMap<(V, T, AA)>,
        addr_to_addr_vec: impl FnOnce(AA) -> AddrVec<A>,
        remove_from_state: impl FnOnce(&mut Bound<S>, I) -> RemoveResult,
    ) -> Option<(V, T, AA)> {
        id_to_sock.remove(id.clone().into()).map(move |(state, sharing_state, addr)| {
            let mut entry = match addr_to_state.entry(addr_to_addr_vec(addr.clone())) {
                Entry::Vacant(_) => unreachable!("state is inconsistent"),
                Entry::Occupied(o) => o,
            };
            match entry.map_mut(|value| remove_from_state(value, id)) {
                RemoveResult::Success => (),
                RemoveResult::IsLast => {
                    let _: Bound<S> = entry.remove();
                }
            }
            (state, sharing_state, addr)
        })
    }

    pub(crate) fn get_shadower_counts(&self, addr: &AddrVec<A>) -> usize {
        let Self { listener_id_to_sock: _, conn_id_to_sock: _, addr_to_state } = self;
        addr_to_state.descendant_counts(&addr).map(|(_sharing, size)| size.get()).sum()
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

    use net_declare::net_ip_v4;
    use net_types::{ip::Ipv4Addr, SpecifiedAddr};

    use crate::{
        ip::DummyDeviceId,
        socket::address::{ConnIpAddr, ListenerIpAddr},
        testutil::set_logger_for_test,
    };

    use super::*;

    enum FakeSpec {}

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

    enum FakeAddrSpec {}

    impl SocketMapAddrSpec for FakeAddrSpec {
        type IpAddr = Ipv4Addr;
        type DeviceId = DummyDeviceId;
        type LocalIdentifier = u16;
        type RemoteIdentifier = ();
    }

    impl SocketMapStateSpec for FakeSpec {
        type AddrVecTag = char;

        type ListenerId = Listener;
        type ConnId = Conn;

        type ListenerState = u8;
        type ListenerSharingState = char;
        type ConnState = u16;
        type ConnSharingState = char;

        type ListenerAddrState = Multiple<Listener>;
        type ConnAddrState = Multiple<Conn>;
    }

    type FakeBoundSocketMap = BoundSocketMap<FakeAddrSpec, FakeSpec>;

    impl<A: Into<AddrVec<FakeAddrSpec>> + Clone, I: Eq>
        SocketMapAddrStateSpec<A, char, I, FakeAddrSpec, FakeSpec> for Multiple<I>
    {
        fn check_for_conflicts(
            new_state: &char,
            addr: &A,
            socketmap: &SocketMap<AddrVec<FakeAddrSpec>, Bound<FakeSpec>>,
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

        fn try_get_dest<'a, 'b>(
            &'b mut self,
            new_state: &'a char,
        ) -> Result<&'b mut Vec<I>, IncompatibleError> {
            let Self(c, v) = self;
            (new_state == c).then(|| v).ok_or(IncompatibleError)
        }

        fn new_addr_state(new_sharing_state: &char, id: I) -> Self {
            Self(*new_sharing_state, vec![id])
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

    const LISTENER_ADDR: ListenerAddr<Ipv4Addr, DummyDeviceId, u16> = ListenerAddr {
        ip: ListenerIpAddr {
            addr: Some(unsafe { SpecifiedAddr::new_unchecked(net_ip_v4!("1.2.3.4")) }),
            identifier: 0,
        },
        device: None,
    };

    const CONN_ADDR: ConnAddr<Ipv4Addr, DummyDeviceId, u16, ()> = ConnAddr {
        ip: unsafe {
            ConnIpAddr {
                local: (SpecifiedAddr::new_unchecked(net_ip_v4!("5.6.7.8")), 0),
                remote: (SpecifiedAddr::new_unchecked(net_ip_v4!("8.7.6.5")), ()),
            }
        },
        device: None,
    };

    #[test]
    fn bound_insert_get_remove_listener() {
        set_logger_for_test();
        let mut bound = FakeBoundSocketMap::default();

        let addr = LISTENER_ADDR;

        let id = bound.try_insert_listener(addr, 0, 'v').unwrap();
        assert_eq!(bound.get_listener_by_id(&id), Some(&(0, 'v', addr)));
        assert_eq!(bound.get_listener_by_addr(&addr), Some(&Multiple('v', vec![id])));

        assert_eq!(bound.remove_listener_by_id(id), Some((0, 'v', addr)));
        assert_eq!(bound.get_listener_by_addr(&addr), None);
        assert_eq!(bound.get_listener_by_id(&id), None);
    }

    #[test]
    fn bound_insert_get_remove_conn() {
        set_logger_for_test();
        let mut bound = FakeBoundSocketMap::default();

        let addr = CONN_ADDR;

        let id = bound.try_insert_conn(addr, 0, 'v').unwrap();
        assert_eq!(bound.get_conn_by_id(&id), Some(&(0, 'v', addr)));
        assert_eq!(bound.get_conn_by_addr(&addr), Some(&Multiple('v', vec![id])));

        assert_eq!(bound.remove_conn_by_id(id), Some((0, 'v', addr)));
        assert_eq!(bound.get_conn_by_addr(&addr), None);
        assert_eq!(bound.get_conn_by_id(&id), None);
    }

    #[test]
    fn bound_iter_addrs() {
        set_logger_for_test();
        let mut bound = FakeBoundSocketMap::default();

        let listener_addrs = [
            (Some(net_ip_v4!("1.1.1.1")), 1),
            (Some(net_ip_v4!("2.2.2.2")), 2),
            (Some(net_ip_v4!("1.1.1.1")), 3),
            (None, 4),
        ]
        .map(|(ip, identifier)| ListenerAddr {
            device: None,
            ip: ListenerIpAddr { addr: ip.map(|x| SpecifiedAddr::new(x).unwrap()), identifier },
        });
        let conn_addrs = [
            (net_ip_v4!("3.3.3.3"), 3, net_ip_v4!("4.4.4.4")),
            (net_ip_v4!("4.4.4.4"), 3, net_ip_v4!("3.3.3.3")),
        ]
        .map(|(local_ip, local_identifier, remote_ip)| ConnAddr {
            ip: ConnIpAddr {
                local: (SpecifiedAddr::new(local_ip).unwrap(), local_identifier),
                remote: (SpecifiedAddr::new(remote_ip).unwrap(), ()),
            },
            device: None,
        });

        for addr in listener_addrs.iter().cloned() {
            let _: Listener = bound.try_insert_listener(addr, 1, 'a').unwrap();
        }
        for addr in conn_addrs.iter().cloned() {
            let _: Conn = bound.try_insert_conn(addr, 1, 'a').unwrap();
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
        let mut bound = FakeBoundSocketMap::default();
        let addr = LISTENER_ADDR;

        let _id = bound.try_insert_listener(addr, 0, 'a').unwrap();
        assert_eq!(bound.try_insert_listener(addr, 0, 'b'), Err((InsertError::Exists, 0, 'b')));
    }

    #[test]
    fn insert_listener_conflict_with_shadower() {
        set_logger_for_test();
        let mut bound = FakeBoundSocketMap::default();
        let addr = LISTENER_ADDR;
        let shadows_addr = {
            assert_eq!(addr.device, None);
            ListenerAddr { device: Some(DummyDeviceId), ..addr }
        };

        let _id = bound.try_insert_listener(addr, 0, 'a').unwrap();
        assert_eq!(
            bound.try_insert_listener(shadows_addr, 0, 'b'),
            Err((InsertError::ShadowAddrExists, 0, 'b'))
        );
    }

    #[test]
    fn insert_conn_conflict_with_listener() {
        set_logger_for_test();
        let mut bound = FakeBoundSocketMap::default();
        let addr = LISTENER_ADDR;
        let shadows_addr = ConnAddr {
            device: None,
            ip: ConnIpAddr {
                local: (addr.ip.addr.unwrap(), addr.ip.identifier),
                remote: (SpecifiedAddr::new(net_ip_v4!("1.1.1.1")).unwrap(), ()),
            },
        };

        let _id = bound.try_insert_listener(addr, 0, 'a').unwrap();
        assert_eq!(
            bound.try_insert_conn(shadows_addr, 0, 'b'),
            Err((InsertError::ShadowAddrExists, 0, 'b'))
        );
    }

    #[test]
    fn insert_and_remove_listener() {
        set_logger_for_test();
        let mut bound = FakeBoundSocketMap::default();
        let addr = LISTENER_ADDR;

        let a = bound.try_insert_listener(addr, 0, 'x').unwrap();
        let b = bound.try_insert_listener(addr, 0, 'x').unwrap();
        assert_ne!(a, b);

        assert_eq!(bound.remove_listener_by_id(a), Some((0, 'x', addr)));
        assert_eq!(bound.get_listener_by_addr(&addr), Some(&Multiple('x', vec![b])));
    }

    #[test]
    fn insert_and_remove_conn() {
        set_logger_for_test();
        let mut bound = FakeBoundSocketMap::default();
        let addr = CONN_ADDR;

        let a = bound.try_insert_conn(addr, 0, 'x').unwrap();
        let b = bound.try_insert_conn(addr, 0, 'x').unwrap();
        assert_ne!(a, b);

        assert_eq!(bound.remove_conn_by_id(a), Some((0, 'x', addr)));
        assert_eq!(bound.get_conn_by_addr(&addr), Some(&Multiple('x', vec![b])));
    }

    #[test]
    fn update_listener_to_shadowed_addr_fails() {
        let mut bound = FakeBoundSocketMap::default();

        let first_addr = LISTENER_ADDR;
        let second_addr = ListenerAddr {
            ip: ListenerIpAddr {
                addr: Some(SpecifiedAddr::new(net_ip_v4!("1.1.1.1")).unwrap()),
                ..LISTENER_ADDR.ip
            },
            ..LISTENER_ADDR
        };
        let both_shadow = ListenerAddr {
            ip: ListenerIpAddr { addr: None, identifier: first_addr.ip.identifier },
            device: None,
        };

        let first = bound.try_insert_listener(first_addr, 0, 'a').unwrap();
        let second = bound.try_insert_listener(second_addr, 0, 'b').unwrap();

        // Moving from (1, "aaa") to (1, None) should fail since it is shadowed
        // by (1, "yyy"), and vise versa.
        assert_eq!(bound.try_update_listener_addr(&second, |_| both_shadow), Err(ExistsError));
        assert_eq!(bound.try_update_listener_addr(&first, |_| both_shadow), Err(ExistsError));
    }

    #[test]
    fn get_listener_by_id_mut() {
        let mut map = FakeBoundSocketMap::default();
        let addr = LISTENER_ADDR;
        let listener_id = map.try_insert_listener(addr.clone(), 0, 'x').expect("failed to insert");
        let (val, _, _) = map.get_listener_by_id_mut(&listener_id).expect("failed to get listener");
        *val = 2;

        assert_eq!(map.remove_listener_by_id(listener_id), Some((2, 'x', addr)));
    }

    #[test]
    fn get_conn_by_id_mut() {
        let mut map = FakeBoundSocketMap::default();
        let addr = CONN_ADDR;
        let conn_id = map.try_insert_conn(addr.clone(), 0, 'a').expect("failed to insert");
        let (val, _, _) = map.get_conn_by_id_mut(&conn_id).expect("failed to get conn");
        *val = 2;

        assert_eq!(map.remove_conn_by_id(conn_id), Some((2, 'a', addr)));
    }
}
