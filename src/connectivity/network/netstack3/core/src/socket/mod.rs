// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! General-purpose socket utilities common to device layer and IP layer
//! sockets.

pub(crate) mod address;
pub mod datagram;
pub(crate) mod posix;

use alloc::{collections::HashMap, vec::Vec};
use core::{fmt::Debug, hash::Hash, marker::PhantomData};
use net_types::{
    ip::{Ip, IpAddress},
    AddrAndZone, SpecifiedAddr, Witness as _,
};

use derivative::Derivative;

use crate::{
    data_structures::{
        id_map::{Entry as IdMapEntry, IdMap, OccupiedEntry as IdMapOccupiedEntry},
        socketmap::{
            Entry, IterShadows, OccupiedEntry as SocketMapOccupiedEntry, SocketMap, Tagged,
        },
    },
    error::ExistsError,
    ip::{IpDeviceId, IpExt},
    socket::address::{ConnAddr, ListenerAddr, ListenerIpAddr},
};

/// Determines whether the provided address is underspecified by itself.
///
/// Some addresses are ambiguous and so must have a zone identifier in order
/// to be used in a socket address. This function returns true for IPv6
/// link-local addresses and false for all others.
pub(crate) fn must_have_zone<A: IpAddress>(addr: &SpecifiedAddr<A>) -> bool {
    try_into_null_zoned(addr).is_some()
}

/// Converts into a [`AddrAndZone<A, ()>`] if the address requires a zone.
///
/// Otherwise returns `None`.
pub(crate) fn try_into_null_zoned<A: IpAddress>(
    addr: &SpecifiedAddr<A>,
) -> Option<AddrAndZone<A, ()>> {
    if addr.get().is_loopback() {
        return None;
    }
    AddrAndZone::new(addr.get(), ())
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
}

impl<A: Eq + Hash, S> Default for ConnSocketMap<A, S> {
    fn default() -> ConnSocketMap<A, S> {
        ConnSocketMap { id_to_sock: IdMap::default(), addr_to_id: HashMap::default() }
    }
}

pub(crate) trait SocketMapAddrSpec {
    /// The version of IP addresses in socket addresses.
    type IpVersion: Ip<Addr = Self::IpAddr> + IpExt;
    /// The type of IP addresses in the socket address.
    type IpAddr: IpAddress<Version = Self::IpVersion>;
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
    type ListenerState: Debug;
    /// The state stored for a listening socket that is used to determine
    /// whether sockets can share an address.
    type ListenerSharingState: Clone + Debug;

    /// The state stored for a connected socket.
    type ConnState;
    /// The state stored for a connected socket that is used to determine
    /// whether sockets can share an address.
    type ConnSharingState: Clone + Debug;

    /// The state stored for a listener socket address.
    type ListenerAddrState: SocketMapAddrStateSpec<Id = Self::ListenerId, SharingState = Self::ListenerSharingState>
        + Debug;

    /// The state stored for a connected socket address.
    type ConnAddrState: SocketMapAddrStateSpec<Id = Self::ConnId, SharingState = Self::ConnSharingState>
        + Debug;
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub(crate) struct IncompatibleError;

pub(crate) trait SocketMapAddrStateSpec {
    type Id;
    type SharingState;
    /// Creates a new `Self` holding the provided socket with the given new
    /// sharing state at the specified address.
    fn new(new_sharing_state: &Self::SharingState, id: Self::Id) -> Self;

    /// Gets the target in the existing socket(s) in `self` for a new socket
    /// with the provided state.
    ///
    /// If the new state is incompatible with the existing socket(s),
    /// implementations of this function should return
    /// `Err(IncompatibleError)`. If `Ok(dest)` is returned, the new socket ID
    /// will be appended to `dest`.
    fn try_get_dest<'a, 'b>(
        &'b mut self,
        new_sharing_state: &'a Self::SharingState,
    ) -> Result<&'b mut Vec<Self::Id>, IncompatibleError>;

    /// Returns `Ok` if an entry with the given sharing state could be added
    /// to `self`.
    ///
    /// If this returns `Ok`, `try_get_dest` should succeed.
    fn could_insert(&self, new_sharing_state: &Self::SharingState)
        -> Result<(), IncompatibleError>;

    /// Removes the given socket from the existing state.
    ///
    /// Implementations should assume that `id` is contained in `self`.
    fn remove_by_id(&mut self, id: Self::Id) -> RemoveResult;
}

pub(crate) trait SocketMapConflictPolicy<Addr, SharingState, A: SocketMapAddrSpec>:
    SocketMapStateSpec
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
        socketmap: &SocketMap<AddrVec<A>, Bound<Self>>,
    ) -> Result<(), InsertError>
    where
        Bound<Self>: Tagged<AddrVec<A>>;
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

#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) enum SocketAddrType {
    AnyListener,
    SpecificListener,
    Connected,
}

#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) struct SocketAddrTypeTag<S> {
    pub(crate) has_device: bool,
    pub(crate) addr_type: SocketAddrType,
    pub(crate) sharing: S,
}

impl<'a, A: IpAddress, D, LI, S> From<(&'a ListenerAddr<A, D, LI>, S)> for SocketAddrTypeTag<S> {
    fn from((addr, sharing): (&'a ListenerAddr<A, D, LI>, S)) -> Self {
        let ListenerAddr { ip: ListenerIpAddr { addr, identifier: _ }, device } = addr;
        SocketAddrTypeTag {
            has_device: device.is_some(),
            addr_type: if addr.is_some() {
                SocketAddrType::SpecificListener
            } else {
                SocketAddrType::AnyListener
            },
            sharing,
        }
    }
}

impl<'a, A: IpAddress, D, LI, RI, S> From<(&'a ConnAddr<A, D, LI, RI>, S)>
    for SocketAddrTypeTag<S>
{
    fn from((addr, sharing): (&'a ConnAddr<A, D, LI, RI>, S)) -> Self {
        let ConnAddr { ip: _, device } = addr;
        SocketAddrTypeTag {
            has_device: device.is_some(),
            addr_type: SocketAddrType::Connected,
            sharing,
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

/// Allows immutable access to the state for a particular type of socket
/// (listener or connected) in a [`BoundSocketMap`].
pub(crate) trait SocketTypeState<'a> {
    type Id;
    type State;
    type SharingState;
    type AddrState;
    type Addr;

    /// Returns the state at an address, if there is any.
    fn get_by_addr(self, addr: &Self::Addr) -> Option<&'a Self::AddrState>;

    /// Returns the state corresponding to an identifier, if it exists.
    fn get_by_id(self, id: &Self::Id) -> Option<&'a (Self::State, Self::SharingState, Self::Addr)>;

    /// Returns `Ok(())` if a socket could be inserted, otherwise an error.
    ///
    /// Goes through a dry run of inserting a socket at the given address and
    /// with the given sharing state, returning `Ok(())` if the insertion would
    /// succeed, otherwise the error that would be returned.
    fn could_insert(
        self,
        addr: &Self::Addr,
        sharing_state: &Self::SharingState,
    ) -> Result<(), InsertError>;
}

/// Allows mutable access to the state for a particular type of socket (listener
/// or connected) in a [`BoundSocketMap`].
pub(crate) trait SocketTypeStateMut<'a> {
    type Id;
    type State;
    type SharingState;
    type Addr;
    type Entry: SocketTypeStateEntry<
            State = Self::State,
            SharingState = Self::SharingState,
            Addr = Self::Addr,
            Id = Self::Id,
        > + Debug;

    fn get_by_id_mut(
        self,
        id: &Self::Id,
    ) -> Option<(&'a mut Self::State, &'a Self::SharingState, &'a Self::Addr)>;

    fn try_insert<S: Into<Self::State>>(
        self,
        addr: Self::Addr,
        state: S,
        sharing_state: Self::SharingState,
    ) -> Result<Self::Entry, (InsertError, S, Self::SharingState)>;

    fn entry(self, id: &Self::Id) -> Option<Self::Entry>;

    fn remove(self, id: &Self::Id) -> Option<(Self::State, Self::SharingState, Self::Addr)>
    where
        Self: Sized,
    {
        self.entry(id).map(SocketTypeStateEntry::remove)
    }
}

pub(crate) trait SocketTypeStateEntry: Sized {
    type Id;
    type State;
    type SharingState;
    type Addr;

    fn get(&self) -> &(Self::State, Self::SharingState, Self::Addr);

    fn id(&self) -> Self::Id;

    fn get_state_mut(&mut self) -> &mut Self::State;

    fn try_update_addr(self, new_addr: Self::Addr) -> Result<Self, (ExistsError, Self)>;

    fn remove(self) -> (Self::State, Self::SharingState, Self::Addr);
}

/// View struct over one type of sockets in a [`BoundSocketMap`].
///
/// Used to implement [`SocketTypeState`] and [`SocketTypeStateMut`] for generic
/// socket types.
struct Sockets<IdToStateMap, AddrToStateMap, Id, AddrState, Convert>(
    IdToStateMap,
    AddrToStateMap,
    PhantomData<(Id, AddrState, Convert)>,
);

impl<
        'a,
        Id: Clone + Into<usize>,
        State,
        SharingState,
        Addr: Debug,
        AddrState: SocketMapAddrStateSpec<SharingState = SharingState> + Debug,
        Convert: ConvertSocketTypeState<A, S, Addr, AddrState>,
        A: SocketMapAddrSpec,
        S: SocketMapStateSpec,
    > SocketTypeState<'a>
    for Sockets<
        &'a IdMap<(State, SharingState, Addr)>,
        &'a SocketMap<AddrVec<A>, Bound<S>>,
        Id,
        AddrState,
        Convert,
    >
where
    Bound<S>: Tagged<AddrVec<A>>,
    S: SocketMapConflictPolicy<Addr, SharingState, A>,
{
    type Id = Id;
    type State = State;
    type SharingState = SharingState;
    type AddrState = AddrState;
    type Addr = Addr;

    fn get_by_addr(self, addr: &Addr) -> Option<&'a AddrState> {
        let Self(_id_to_sock, addr_to_state, _marker) = self;
        addr_to_state.get(&Convert::to_addr_vec(addr)).map(|state| {
            Convert::from_bound_ref(state)
                .unwrap_or_else(|| unreachable!("found {:?} for address {:?}", state, addr))
        })
    }

    fn get_by_id(self, id: &Id) -> Option<&'a (State, SharingState, Addr)> {
        let Self(id_to_sock, _addr_to_state, _) = self;
        id_to_sock.get(id.clone().into())
    }

    fn could_insert(self, addr: &Addr, sharing: &SharingState) -> Result<(), InsertError> {
        let Self(_, addr_to_state, _) = self;
        match self.get_by_addr(addr) {
            Some(state) => {
                state.could_insert(sharing).map_err(|IncompatibleError| InsertError::Exists)
            }
            None => S::check_for_conflicts(&sharing, &addr, &addr_to_state),
        }
    }
}

#[derive(Derivative)]
#[derivative(Debug(bound = "A: Debug"))]
struct SocketStateEntry<'a, IdV, A: Eq + Hash, S: Tagged<A>, AddrState, Convert> {
    id_entry: IdMapOccupiedEntry<'a, usize, IdV>,
    addr_entry: SocketMapOccupiedEntry<'a, A, S>,
    _marker: PhantomData<(AddrState, Convert)>,
}

impl<
        'a,
        State,
        Addr: Clone + Debug,
        AddrState: SocketMapAddrStateSpec,
        Convert: ConvertSocketTypeState<A, S, Addr, AddrState>,
        A: SocketMapAddrSpec,
        S: SocketMapStateSpec + SocketMapConflictPolicy<Addr, AddrState::SharingState, A>,
    > SocketTypeStateMut<'a>
    for Sockets<
        &'a mut IdMap<(State, AddrState::SharingState, Addr)>,
        &'a mut SocketMap<AddrVec<A>, Bound<S>>,
        AddrState::Id,
        AddrState,
        Convert,
    >
where
    Bound<S>: Tagged<AddrVec<A>>,
    AddrState::Id: Clone + From<usize> + Into<usize>,
{
    type Id = AddrState::Id;
    type State = State;
    type SharingState = AddrState::SharingState;
    type Addr = Addr;
    type Entry = SocketStateEntry<
        'a,
        (State, AddrState::SharingState, Addr),
        AddrVec<A>,
        Bound<S>,
        AddrState,
        Convert,
    >;

    fn get_by_id_mut(
        self,
        id: &Self::Id,
    ) -> Option<(&'a mut Self::State, &'a Self::SharingState, &'a Self::Addr)> {
        let Self(id_to_sock, _addr_to_state, _) = self;
        id_to_sock
            .get_mut(id.clone().into())
            .map(|(state, tag_state, addr)| (state, &*tag_state, &*addr))
    }

    fn try_insert<St: Into<Self::State>>(
        self,
        socket_addr: Self::Addr,
        state: St,
        tag_state: Self::SharingState,
    ) -> Result<Self::Entry, (InsertError, St, Self::SharingState)> {
        let Self(id_to_sock, addr_to_state, _) = self;
        match S::check_for_conflicts(&tag_state, &socket_addr, &addr_to_state) {
            Err(e) => return Err((e, state, tag_state)),
            Ok(()) => (),
        };

        let addr = Convert::to_addr_vec(&socket_addr);

        match addr_to_state.entry(addr) {
            Entry::Occupied(mut o) => {
                let id_entry = o.map_mut(|bound| {
                    let bound = match Convert::from_bound_mut(bound) {
                        Some(bound) => bound,
                        None => unreachable!("found {:?} for address {:?}", bound, socket_addr),
                    };
                    match AddrState::try_get_dest(bound, &tag_state) {
                        Ok(v) => {
                            let idmap_entry =
                                id_to_sock.push_entry((state.into(), tag_state, socket_addr));
                            v.push(idmap_entry.key().clone().into());
                            Ok(idmap_entry)
                        }
                        Err(IncompatibleError) => Err((InsertError::Exists, state, tag_state)),
                    }
                })?;
                Ok(SocketStateEntry { id_entry, addr_entry: o, _marker: Default::default() })
            }
            Entry::Vacant(v) => {
                let id_entry = id_to_sock.push_entry((state.into(), tag_state, socket_addr));
                let (_state, tag_state, _addr): &(Self::State, _, Self::Addr) = id_entry.get();
                let addr_entry = v.insert(Convert::to_bound(AddrState::new(
                    tag_state,
                    id_entry.key().clone().into(),
                )));
                Ok(SocketStateEntry { id_entry, addr_entry, _marker: Default::default() })
            }
        }
    }

    fn entry(self, id: &Self::Id) -> Option<Self::Entry> {
        let Self(id_to_sock, addr_to_state, _) = self;
        let id_entry = match id_to_sock.entry(id.clone().into()) {
            IdMapEntry::Vacant(_) => return None,
            IdMapEntry::Occupied(o) => o,
        };
        let (_, _, addr): &(Self::State, Self::SharingState, _) = id_entry.get();
        let addr_entry = match addr_to_state.entry(Convert::to_addr_vec(addr)) {
            Entry::Vacant(_) => unreachable!("state is inconsistent"),
            Entry::Occupied(o) => o,
        };
        Some(SocketStateEntry { id_entry, addr_entry, _marker: PhantomData::default() })
    }
}

impl<
        'a,
        State,
        SharingState,
        Addr: Debug,
        AddrState: SocketMapAddrStateSpec,
        Convert: ConvertSocketTypeState<A, S, Addr, AddrState>,
        A: SocketMapAddrSpec,
        S: SocketMapStateSpec,
    > SocketTypeStateEntry
    for SocketStateEntry<'a, (State, SharingState, Addr), AddrVec<A>, Bound<S>, AddrState, Convert>
where
    Bound<S>: Tagged<AddrVec<A>>,
    AddrState::Id: From<usize>,
{
    type Id = AddrState::Id;
    type State = State;
    type SharingState = SharingState;
    type Addr = Addr;

    fn get(&self) -> &(Self::State, Self::SharingState, Self::Addr) {
        let Self { id_entry, addr_entry: _, _marker } = self;
        id_entry.get()
    }

    fn id(&self) -> AddrState::Id {
        let Self { id_entry, addr_entry: _, _marker } = self;
        id_entry.key().clone().into()
    }

    fn get_state_mut(&mut self) -> &mut Self::State {
        let Self { id_entry, addr_entry: _, _marker } = self;
        let (state, _, _): &mut (_, SharingState, Addr) = id_entry.get_mut();
        state
    }

    fn try_update_addr(self, new_addr: Self::Addr) -> Result<Self, (ExistsError, Self)> {
        let Self { mut id_entry, addr_entry, _marker } = self;

        let new_addrvec = Convert::to_addr_vec(&new_addr);
        let (addr_state, addr_to_state) = addr_entry.remove_from_map();
        let addr_to_state = match addr_to_state.entry(new_addrvec) {
            Entry::Occupied(o) => o.into_map(),
            Entry::Vacant(v) => {
                if v.descendant_counts().len() != 0 {
                    v.into_map()
                } else {
                    let new_addr_entry = v.insert(addr_state);
                    let (_, _, addr): &mut (Self::State, Self::SharingState, _) =
                        id_entry.get_mut();
                    *addr = new_addr;
                    return Ok(SocketStateEntry { id_entry, addr_entry: new_addr_entry, _marker });
                }
            }
        };
        let to_restore = addr_state;

        let (_, _, addr): &(Self::State, Self::SharingState, _) = id_entry.get();
        let addrvec = Convert::to_addr_vec(&addr);

        // Restore the old state before returning an error.
        let addr_entry = match addr_to_state.entry(addrvec) {
            Entry::Occupied(_) => unreachable!("just-removed-from entry is occupied"),
            Entry::Vacant(v) => v.insert(to_restore),
        };
        return Err((ExistsError, SocketStateEntry { id_entry, addr_entry, _marker }));
    }

    fn remove(self) -> (Self::State, Self::SharingState, Self::Addr) {
        let Self { id_entry, mut addr_entry, _marker } = self;
        let id = *id_entry.key();
        let (state, tag_state, addr) = id_entry.remove();
        match addr_entry.map_mut(|value| {
            let value = match Convert::from_bound_mut(value) {
                Some(value) => value,
                None => unreachable!("found {:?} for address {:?}", value, addr),
            };
            value.remove_by_id(id.clone().into())
        }) {
            RemoveResult::Success => (),
            RemoveResult::IsLast => {
                let _: Bound<S> = addr_entry.remove();
            }
        }
        (state, tag_state, addr)
    }
}

impl<A: SocketMapAddrSpec, S> BoundSocketMap<A, S>
where
    Bound<S>: Tagged<AddrVec<A>>,
    AddrVec<A>: IterShadows,
    S: SocketMapStateSpec,
{
    pub(crate) fn listeners(
        &self,
    ) -> impl SocketTypeState<
        '_,
        Id = S::ListenerId,
        State = S::ListenerState,
        SharingState = S::ListenerSharingState,
        AddrState = S::ListenerAddrState,
        Addr = ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
    >
    where
        S: SocketMapConflictPolicy<
            ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
            <S as SocketMapStateSpec>::ListenerSharingState,
            A,
        >,
        S::ListenerAddrState:
            SocketMapAddrStateSpec<Id = S::ListenerId, SharingState = S::ListenerSharingState>,
    {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_state } = self;
        Sockets::<_, _, S::ListenerId, S::ListenerAddrState, Self>(
            listener_id_to_sock,
            addr_to_state,
            Default::default(),
        )
    }

    pub(crate) fn listeners_mut(
        &mut self,
    ) -> impl SocketTypeStateMut<
        '_,
        Id = S::ListenerId,
        State = S::ListenerState,
        SharingState = S::ListenerSharingState,
        Addr = ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
    >
    where
        S: SocketMapConflictPolicy<
            ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
            <S as SocketMapStateSpec>::ListenerSharingState,
            A,
        >,
        S::ListenerAddrState:
            SocketMapAddrStateSpec<Id = S::ListenerId, SharingState = S::ListenerSharingState>,
    {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_state } = self;
        Sockets::<_, _, S::ListenerId, S::ListenerAddrState, Self>(
            listener_id_to_sock,
            addr_to_state,
            Default::default(),
        )
    }

    pub(crate) fn conns(
        &self,
    ) -> impl SocketTypeState<
        '_,
        Id = S::ConnId,
        State = S::ConnState,
        SharingState = S::ConnSharingState,
        AddrState = S::ConnAddrState,
        Addr = ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
    >
    where
        S: SocketMapConflictPolicy<
            ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
            <S as SocketMapStateSpec>::ConnSharingState,
            A,
        >,
        S::ConnAddrState:
            SocketMapAddrStateSpec<Id = S::ConnId, SharingState = S::ConnSharingState>,
    {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_state } = self;
        Sockets::<_, _, S::ConnId, S::ConnAddrState, Self>(
            conn_id_to_sock,
            addr_to_state,
            Default::default(),
        )
    }

    pub(crate) fn conns_mut(
        &mut self,
    ) -> impl SocketTypeStateMut<
        '_,
        Id = S::ConnId,
        State = S::ConnState,
        SharingState = S::ConnSharingState,
        Addr = ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
    >
    where
        S: SocketMapConflictPolicy<
            ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
            <S as SocketMapStateSpec>::ConnSharingState,
            A,
        >,
        S::ConnAddrState:
            SocketMapAddrStateSpec<Id = S::ConnId, SharingState = S::ConnSharingState>,
    {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_state } = self;
        Sockets::<_, _, S::ConnId, S::ConnAddrState, Self>(
            conn_id_to_sock,
            addr_to_state,
            Default::default(),
        )
    }

    #[cfg(test)]
    pub(crate) fn iter_addrs(&self) -> impl Iterator<Item = &AddrVec<A>> {
        let Self { listener_id_to_sock: _, conn_id_to_sock: _, addr_to_state } = self;
        addr_to_state.iter().map(|(a, _v): (_, &Bound<S>)| a)
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

/// Helper trait for converting between [`AddrVec`] and [`Bound`] and their
/// variants.
trait ConvertSocketTypeState<A: SocketMapAddrSpec, S: SocketMapStateSpec, Addr, AddrState> {
    fn to_addr_vec(addr: &Addr) -> AddrVec<A>;
    fn from_bound_ref(bound: &Bound<S>) -> Option<&AddrState>;
    fn from_bound_mut(bound: &mut Bound<S>) -> Option<&mut AddrState>;
    fn to_bound(state: AddrState) -> Bound<S>;
}

impl<A: SocketMapAddrSpec, S: SocketMapStateSpec>
    ConvertSocketTypeState<
        A,
        S,
        ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
        S::ListenerAddrState,
    > for BoundSocketMap<A, S>
where
    Bound<S>: Tagged<AddrVec<A>>,
{
    fn to_addr_vec(addr: &ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>) -> AddrVec<A> {
        AddrVec::Listen(addr.clone())
    }

    fn from_bound_ref(bound: &Bound<S>) -> Option<&S::ListenerAddrState> {
        match bound {
            Bound::Listen(l) => Some(l),
            Bound::Conn(_c) => None,
        }
    }

    fn from_bound_mut(bound: &mut Bound<S>) -> Option<&mut S::ListenerAddrState> {
        match bound {
            Bound::Listen(l) => Some(l),
            Bound::Conn(_c) => None,
        }
    }

    fn to_bound(state: S::ListenerAddrState) -> Bound<S> {
        Bound::Listen(state)
    }
}

impl<A: SocketMapAddrSpec, S: SocketMapStateSpec>
    ConvertSocketTypeState<
        A,
        S,
        ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
        S::ConnAddrState,
    > for BoundSocketMap<A, S>
where
    Bound<S>: Tagged<AddrVec<A>>,
{
    fn to_addr_vec(
        addr: &ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
    ) -> AddrVec<A> {
        AddrVec::Conn(addr.clone())
    }

    fn from_bound_ref(bound: &Bound<S>) -> Option<&S::ConnAddrState> {
        match bound {
            Bound::Listen(_l) => None,
            Bound::Conn(c) => Some(c),
        }
    }

    fn from_bound_mut(bound: &mut Bound<S>) -> Option<&mut S::ConnAddrState> {
        match bound {
            Bound::Listen(_l) => None,
            Bound::Conn(c) => Some(c),
        }
    }

    fn to_bound(state: S::ConnAddrState) -> Bound<S> {
        Bound::Conn(state)
    }
}

#[cfg(test)]
mod tests {
    use alloc::{collections::HashSet, vec, vec::Vec};

    use assert_matches::assert_matches;
    use net_declare::{net_ip_v4, net_ip_v6};
    use net_types::{
        ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
        SpecifiedAddr,
    };
    use test_case::test_case;

    use crate::{
        ip::testutil::FakeDeviceId,
        socket::address::{ConnIpAddr, ListenerIpAddr},
        testutil::set_logger_for_test,
    };

    use super::*;

    #[test_case(net_ip_v4!("8.8.8.8"))]
    #[test_case(net_ip_v4!("127.0.0.1"))]
    #[test_case(net_ip_v4!("127.0.8.9"))]
    #[test_case(net_ip_v4!("224.1.2.3"))]
    fn must_never_have_zone_ipv4(addr: Ipv4Addr) {
        // No IPv4 addresses are allowed to have a zone.
        let addr = SpecifiedAddr::new(addr).unwrap();
        assert_eq!(must_have_zone(&addr), false);
    }

    #[test_case(net_ip_v6!("1::2:3"), false)]
    #[test_case(net_ip_v6!("::1"), false; "localhost")]
    #[test_case(net_ip_v6!("1::"), false)]
    #[test_case(net_ip_v6!("ff03:1:2:3::1"), false)]
    #[test_case(net_ip_v6!("ff02:1:2:3::1"), true)]
    #[test_case(Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(), true)]
    #[test_case(net_ip_v6!("fe80::1"), true)]
    fn must_have_zone_ipv6(addr: Ipv6Addr, must_have: bool) {
        // Only link-local unicast and multicast addresses are allowed to have
        // zones.
        let addr = SpecifiedAddr::new(addr).unwrap();
        assert_eq!(must_have_zone(&addr), must_have);
    }

    #[test]
    fn try_into_null_zoned_ipv6() {
        assert_eq!(try_into_null_zoned(&Ipv6::LOOPBACK_ADDRESS), None);
        let zoned = Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.into_specified();
        const ZONE: u32 = 5;
        assert_eq!(
            try_into_null_zoned(&zoned).map(|a| a.map_zone(|()| ZONE)),
            Some(AddrAndZone::new(zoned.get(), ZONE).unwrap())
        );
    }

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
        type IpVersion = Ipv4;
        type IpAddr = Ipv4Addr;
        type DeviceId = FakeDeviceId;
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

    impl<I: Eq> SocketMapAddrStateSpec for Multiple<I> {
        type Id = I;
        type SharingState = char;

        fn new(new_sharing_state: &char, id: I) -> Self {
            Self(*new_sharing_state, vec![id])
        }

        fn try_get_dest<'a, 'b>(
            &'b mut self,
            new_state: &'a char,
        ) -> Result<&'b mut Vec<I>, IncompatibleError> {
            let Self(c, v) = self;
            (new_state == c).then_some(v).ok_or(IncompatibleError)
        }

        fn could_insert(
            &self,
            new_sharing_state: &Self::SharingState,
        ) -> Result<(), IncompatibleError> {
            let Self(c, _) = self;
            (new_sharing_state == c).then_some(()).ok_or(IncompatibleError)
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

    impl<A: Into<AddrVec<FakeAddrSpec>> + Clone> SocketMapConflictPolicy<A, char, FakeAddrSpec>
        for FakeSpec
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
    }

    const LISTENER_ADDR: ListenerAddr<Ipv4Addr, FakeDeviceId, u16> = ListenerAddr {
        ip: ListenerIpAddr {
            addr: Some(unsafe { SpecifiedAddr::new_unchecked(net_ip_v4!("1.2.3.4")) }),
            identifier: 0,
        },
        device: None,
    };

    const CONN_ADDR: ConnAddr<Ipv4Addr, FakeDeviceId, u16, ()> = ConnAddr {
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

        let id = {
            let entry = bound.listeners_mut().try_insert(addr, 0, 'v').unwrap();
            assert_eq!(entry.get(), &(0, 'v', addr));
            entry.id()
        };

        assert_eq!(bound.listeners().get_by_id(&id), Some(&(0, 'v', addr)));
        assert_eq!(bound.listeners().get_by_addr(&addr), Some(&Multiple('v', vec![id])));

        assert_eq!(bound.listeners_mut().remove(&id), Some((0, 'v', addr)));
        assert_eq!(bound.listeners().get_by_addr(&addr), None);
        assert_eq!(bound.listeners().get_by_id(&id), None);
    }

    #[test]
    fn bound_insert_get_remove_conn() {
        set_logger_for_test();
        let mut bound = FakeBoundSocketMap::default();

        let addr = CONN_ADDR;

        let id = {
            let entry = bound.conns_mut().try_insert(addr, 0u16, 'v').unwrap();
            assert_eq!(entry.get(), &(0, 'v', addr));
            entry.id()
        };

        assert_eq!(bound.conns().get_by_id(&id), Some(&(0, 'v', addr)));
        assert_eq!(bound.conns().get_by_addr(&addr), Some(&Multiple('v', vec![id])));

        assert_eq!(bound.conns_mut().remove(&id), Some((0, 'v', addr)));
        assert_eq!(bound.conns().get_by_addr(&addr), None);
        assert_eq!(bound.conns().get_by_id(&id), None);
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
            let _entry = bound.listeners_mut().try_insert(addr, 1u8, 'a').unwrap();
        }
        for addr in conn_addrs.iter().cloned() {
            let _entry = bound.conns_mut().try_insert(addr, 1u16, 'a').unwrap();
        }
        let expected_addrs = listener_addrs
            .into_iter()
            .map(Into::into)
            .chain(conn_addrs.into_iter().map(Into::into))
            .collect::<HashSet<_>>();

        assert_eq!(expected_addrs, bound.iter_addrs().cloned().collect());
    }

    #[test]
    fn insert_listener_conflict_with_listener() {
        set_logger_for_test();
        let mut bound = FakeBoundSocketMap::default();
        let addr = LISTENER_ADDR;

        let _: Listener = bound.listeners_mut().try_insert(addr, 0, 'a').unwrap().id();
        assert_matches!(
            bound.listeners_mut().try_insert(addr, 0, 'b'),
            Err((InsertError::Exists, 0, 'b'))
        );
    }

    #[test]
    fn insert_listener_conflict_with_shadower() {
        set_logger_for_test();
        let mut bound = FakeBoundSocketMap::default();
        let addr = LISTENER_ADDR;
        let shadows_addr = {
            assert_eq!(addr.device, None);
            ListenerAddr { device: Some(FakeDeviceId), ..addr }
        };

        let _: Listener = bound.listeners_mut().try_insert(addr, 0, 'a').unwrap().id();
        assert_matches!(
            bound.listeners_mut().try_insert(shadows_addr, 0, 'b'),
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

        let _: Listener = bound.listeners_mut().try_insert(addr, 0u8, 'a').unwrap().id();
        assert_matches!(
            bound.conns_mut().try_insert(shadows_addr, 0u16, 'b'),
            Err((InsertError::ShadowAddrExists, 0, 'b'))
        );
    }

    #[test]
    fn insert_and_remove_listener() {
        set_logger_for_test();
        let mut bound = FakeBoundSocketMap::default();
        let addr = LISTENER_ADDR;

        let a = bound.listeners_mut().try_insert(addr, 0, 'x').unwrap().id();
        let b = bound.listeners_mut().try_insert(addr, 0, 'x').unwrap().id();
        assert_ne!(a, b);

        assert_eq!(bound.listeners_mut().remove(&a), Some((0, 'x', addr)));
        assert_eq!(bound.listeners().get_by_addr(&addr), Some(&Multiple('x', vec![b])));
    }

    #[test]
    fn insert_and_remove_conn() {
        set_logger_for_test();
        let mut bound = FakeBoundSocketMap::default();
        let addr = CONN_ADDR;

        let a = bound.conns_mut().try_insert(addr, 0u16, 'x').unwrap().id();
        let b = bound.conns_mut().try_insert(addr, 0u16, 'x').unwrap().id();
        assert_ne!(a, b);

        assert_eq!(bound.conns_mut().remove(&a), Some((0, 'x', addr)));
        assert_eq!(bound.conns().get_by_addr(&addr), Some(&Multiple('x', vec![b])));
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

        let first = bound.listeners_mut().try_insert(first_addr, 0u8, 'a').unwrap().id();
        let second = bound.listeners_mut().try_insert(second_addr, 0u8, 'b').unwrap().id();

        // Moving from (1, "aaa") to (1, None) should fail since it is shadowed
        // by (1, "yyy"), and vise versa.
        let (ExistsError, entry) = bound
            .listeners_mut()
            .entry(&second)
            .unwrap()
            .try_update_addr(both_shadow)
            .expect_err("update should fail");

        // The entry should correspond to `second`.
        assert_eq!(entry.get(), &(0, 'b', second_addr));
        drop(entry);

        let (ExistsError, entry) = bound
            .listeners_mut()
            .entry(&first)
            .unwrap()
            .try_update_addr(both_shadow)
            .expect_err("update should fail");
        assert_eq!(entry.get(), &(0, 'a', first_addr));
    }

    #[test]
    fn get_listeners_by_id_mut() {
        let mut map = FakeBoundSocketMap::default();
        let addr = LISTENER_ADDR;
        let listener_id =
            map.listeners_mut().try_insert(addr.clone(), 0u8, 'x').expect("failed to insert").id();
        let (val, _, _) =
            map.listeners_mut().get_by_id_mut(&listener_id).expect("failed to get listener");
        *val = 2;

        assert_eq!(map.listeners_mut().remove(&listener_id), Some((2, 'x', addr)));
    }

    #[test]
    fn get_conn_by_id_mut() {
        let mut map = FakeBoundSocketMap::default();
        let addr = CONN_ADDR;
        let conn_id =
            map.conns_mut().try_insert(addr.clone(), 0u16, 'a').expect("failed to insert").id();
        let (val, _, _) = map.conns_mut().get_by_id_mut(&conn_id).expect("failed to get conn");
        *val = 2;

        assert_eq!(map.conns_mut().remove(&conn_id), Some((2, 'a', addr)));
    }

    #[test]
    fn nonexistent_conn_entry() {
        let mut map = FakeBoundSocketMap::default();
        let addr = CONN_ADDR;
        let conn_id =
            map.conns_mut().try_insert(addr.clone(), 0u16, 'a').expect("failed to insert").id();
        assert_matches!(map.conns_mut().remove(&conn_id), Some((0, 'a', CONN_ADDR)));

        assert!(map.conns_mut().entry(&conn_id).is_none());
    }
}
