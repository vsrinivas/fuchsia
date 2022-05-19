// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of POSIX-style socket conflict detection semantics on top
//! of [`SocketMapSpec`] that can be used to implement multiple types of
//! sockets.

use alloc::vec::Vec;
use core::{fmt::Debug, hash::Hash, num::NonZeroUsize};

use derivative::Derivative;
use net_types::{ip::IpAddress, SpecifiedAddr};

use crate::{
    data_structures::socketmap::{IterShadows, SocketMap, Tagged},
    socket::{AddrVec, Bound, InsertError, RemoveResult, SocketMapAddrStateSpec, SocketMapSpec},
};

/// Describes the data types associated with types of network POSIX sockets.
///
/// Implementers of this trait get a free implementation of [`SocketMapSpec`]
/// with address vectors composed of the provided address and identifier types.
pub(crate) trait PosixSocketMapSpec {
    type IpAddress: IpAddress;
    /// The type of a remote address.
    type RemoteAddr: Clone + Debug + Eq + Hash;
    /// The type of a local identifier for an address, typically a port (or
    /// ICMP stream ID).
    type LocalIdentifier: Clone + Debug + Eq + Hash;
    /// An identifier for a local device.
    type DeviceId: Clone + Debug + Hash + PartialEq;

    /// An identifier for a listening socket.
    type ListenerId: Clone + Into<usize> + From<usize> + Debug + PartialEq;
    /// An identifier for a connected socket.
    type ConnId: Clone + Into<usize> + From<usize> + Debug + PartialEq;

    /// The state for a listening socket.
    type ListenerState;
    /// The state for a connected socket.
    type ConnState;
}

/// The address vector of a listening socket.
#[derive(Derivative)]
#[derivative(
    Clone(bound = ""),
    Debug(bound = ""),
    Hash(bound = ""),
    Eq(bound = ""),
    PartialEq(bound = "")
)]
pub(crate) struct ListenerAddr<P: PosixSocketMapSpec> {
    pub(crate) ip: ListenerIpAddr<P>,
    pub(crate) device: Option<P::DeviceId>,
}

/// The IP addresses and port of a listening socket.
#[derive(Derivative)]
#[derivative(
    Clone(bound = ""),
    Debug(bound = ""),
    Hash(bound = ""),
    Eq(bound = ""),
    PartialEq(bound = "")
)]
pub(crate) struct ListenerIpAddr<P: PosixSocketMapSpec> {
    /// The specific address being listened on, or `None` for all addresses.
    pub(crate) addr: Option<SpecifiedAddr<P::IpAddress>>,
    pub(crate) identifier: P::LocalIdentifier,
}

impl<P: PosixSocketMapSpec> From<ListenerIpAddr<P>> for PosixIpAddrVec<P> {
    fn from(listener: ListenerIpAddr<P>) -> Self {
        PosixIpAddrVec::Listener(listener)
    }
}

impl<P: PosixSocketMapSpec> From<ConnIpAddr<P>> for PosixIpAddrVec<P> {
    fn from(conn: ConnIpAddr<P>) -> Self {
        PosixIpAddrVec::Connected(conn)
    }
}

/// The address vector of a connected socket.
#[derive(Derivative)]
#[derivative(
    Clone(bound = ""),
    Debug(bound = ""),
    Hash(bound = ""),
    Eq(bound = ""),
    PartialEq(bound = "")
)]
pub(crate) struct ConnAddr<P: PosixSocketMapSpec> {
    pub(crate) ip: ConnIpAddr<P>,
    pub(crate) device: Option<P::DeviceId>,
}

/// The IP addresses and port of a connected socket.
#[derive(Derivative)]
#[derivative(
    Clone(bound = ""),
    Debug(bound = ""),
    Hash(bound = ""),
    Eq(bound = ""),
    PartialEq(bound = "")
)]
pub(crate) struct ConnIpAddr<P: PosixSocketMapSpec> {
    pub(crate) local_ip: SpecifiedAddr<P::IpAddress>,
    pub(crate) local_identifier: P::LocalIdentifier,
    pub(crate) remote: P::RemoteAddr,
}

impl<P: PosixSocketMapSpec> From<ListenerAddr<P>> for AddrVec<P> {
    fn from(listener: ListenerAddr<P>) -> Self {
        AddrVec::Listen(listener)
    }
}

impl<P: PosixSocketMapSpec> From<ConnAddr<P>> for AddrVec<P> {
    fn from(conn: ConnAddr<P>) -> Self {
        AddrVec::Conn(conn)
    }
}

/// An address vector containing the portions of a socket address that are
/// visible in an IP packet.
#[derive(Derivative)]
#[derivative(
    Debug(bound = ""),
    Clone(bound = ""),
    Eq(bound = ""),
    PartialEq(bound = ""),
    Hash(bound = "")
)]
pub(crate) enum PosixIpAddrVec<P: PosixSocketMapSpec> {
    Listener(ListenerIpAddr<P>),
    Connected(ConnIpAddr<P>),
}

impl<P: PosixSocketMapSpec> PosixIpAddrVec<P> {
    fn with_device(self, device: Option<P::DeviceId>) -> AddrVec<P> {
        match self {
            PosixIpAddrVec::Listener(ip) => AddrVec::Listen(ListenerAddr { ip, device }),
            PosixIpAddrVec::Connected(ip) => AddrVec::Conn(ConnAddr { ip, device }),
        }
    }
}

impl<P: PosixSocketMapSpec> PosixIpAddrVec<P> {
    /// Returns the next smallest address vector that would receive all the same
    /// packets as this one.
    ///
    /// Address vectors are ordered by their shadowing relationship, such that
    /// a "smaller" vector shadows a "larger" one. This function returns the
    /// smallest of the set of shadows of `self`.
    fn widen(self) -> Option<Self> {
        match self {
            PosixIpAddrVec::Listener(ListenerIpAddr { addr: None, identifier }) => {
                let _: P::LocalIdentifier = identifier;
                None
            }
            PosixIpAddrVec::Connected(ConnIpAddr { local_ip, local_identifier, remote }) => {
                let _: P::RemoteAddr = remote;
                Some(ListenerIpAddr { addr: Some(local_ip), identifier: local_identifier })
            }
            PosixIpAddrVec::Listener(ListenerIpAddr { addr: Some(addr), identifier }) => {
                let _: SpecifiedAddr<P::IpAddress> = addr;
                Some(ListenerIpAddr { addr: None, identifier })
            }
        }
        .map(PosixIpAddrVec::Listener)
    }
}

/// An iterator over socket addresses.
///
/// The generated address vectors are ordered according to the following
/// rules (ordered by precedence):
///   - a connected address is preferred over a listening address,
///   - a listening address for a specific IP address is preferred over one
///     for all addresses,
///   - an address with a specific device is preferred over one for all
///     devices.
///
/// The first yielded address is always the one provided via
/// [`AddrVecIter::with_device`] or [`AddrVecIter::without_device`].
enum AddrVecIter<P: PosixSocketMapSpec> {
    WithDevice { device: P::DeviceId, emitted_device: bool, addr: PosixIpAddrVec<P> },
    NoDevice { addr: PosixIpAddrVec<P> },
    Done,
}

pub(crate) struct PosixAddrVecIter<P: PosixSocketMapSpec>(AddrVecIter<P>);

impl<P: PosixSocketMapSpec> Iterator for PosixAddrVecIter<P> {
    type Item = AddrVec<P>;

    fn next(&mut self) -> Option<Self::Item> {
        let Self(it) = self;
        it.next()
    }
}

impl<P: PosixSocketMapSpec> AddrVecIter<P> {
    fn with_device(addr: PosixIpAddrVec<P>, device: P::DeviceId) -> Self {
        AddrVecIter::WithDevice { device, emitted_device: false, addr }
    }

    fn without_device(addr: PosixIpAddrVec<P>) -> Self {
        AddrVecIter::NoDevice { addr }
    }
}

impl<P: PosixSocketMapSpec> PosixAddrVecIter<P> {
    pub(crate) fn with_device(addr: impl Into<PosixIpAddrVec<P>>, device: P::DeviceId) -> Self {
        Self(AddrVecIter::with_device(addr.into(), device))
    }
}

impl<P: PosixSocketMapSpec> Iterator for AddrVecIter<P> {
    type Item = AddrVec<P>;

    fn next(&mut self) -> Option<Self::Item> {
        match self {
            AddrVecIter::Done => None,
            AddrVecIter::WithDevice { device, emitted_device, addr } => {
                if !*emitted_device {
                    *emitted_device = true;
                    Some(addr.clone().with_device(Some(device.clone())))
                } else {
                    let r = addr.clone().with_device(None);
                    if let Some(next) = addr.clone().widen() {
                        *addr = next;
                        *emitted_device = false;
                    } else {
                        *self = AddrVecIter::Done;
                    }
                    Some(r)
                }
            }
            AddrVecIter::NoDevice { addr } => {
                let r = addr.clone().with_device(None);
                if let Some(next) = addr.clone().widen() {
                    *addr = next;
                } else {
                    *self = AddrVecIter::Done
                }
                Some(r)
            }
        }
    }
}

impl<P: PosixSocketMapSpec> IterShadows for AddrVec<P> {
    type IterShadows = PosixAddrVecIter<P>;

    fn iter_shadows(&self) -> Self::IterShadows {
        let (socket_ip_addr, device) = match self.clone() {
            AddrVec::Conn(ConnAddr { ip, device }) => (ip.into(), device),
            AddrVec::Listen(ListenerAddr { ip, device }) => (ip.into(), device),
        };
        let mut iter = match device {
            Some(device) => AddrVecIter::with_device(socket_ip_addr, device),
            None => AddrVecIter::without_device(socket_ip_addr),
        };
        // Skip the first element, which is always `*self`.
        assert_eq!(iter.next().as_ref(), Some(self));
        PosixAddrVecIter(iter)
    }
}

#[derive(Debug)]
pub(crate) enum PosixAddrState<T> {
    Exclusive(T),
    ExclusiveDevice(T),
}

#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) enum PosixAddrVecTag {
    Exclusive,
    ExclusiveDevice,
}

impl<T> Tagged for PosixAddrState<T> {
    type Tag = PosixAddrVecTag;
    fn tag(&self) -> Self::Tag {
        match self {
            PosixAddrState::Exclusive(_) => PosixAddrVecTag::Exclusive,
            PosixAddrState::ExclusiveDevice(_) => PosixAddrVecTag::ExclusiveDevice,
        }
    }
}

impl<P: PosixSocketMapSpec> SocketMapSpec for P {
    type ListenerAddr = ListenerAddr<P>;

    type ConnAddr = ConnAddr<P>;

    type AddrVecTag = PosixAddrVecTag;

    type ListenerId = P::ListenerId;

    type ConnId = P::ConnId;

    type ListenerState = P::ListenerState;

    type ConnState = P::ConnState;

    type ListenerAddrState = PosixAddrState<P::ListenerId>;

    type ConnAddrState = PosixAddrState<P::ConnId>;
}

impl<A, St, I, P> SocketMapAddrStateSpec<A, St, I, P> for PosixAddrState<I>
where
    P: PosixSocketMapSpec,
    A: Clone + Into<AddrVec<P>>,
    I: PartialEq,
{
    fn check_for_conflicts(
        _new_state: &St,
        addr: &A,
        socketmap: &SocketMap<AddrVec<P>, Bound<P>>,
    ) -> Result<(), InsertError> {
        let dest = addr.clone().into();
        // Having a value present at a shadowed address is immediately
        // disqualifying.
        if dest.iter_shadows().any(|a| socketmap.get(&a).is_some()) {
            return Err(InsertError::ShadowAddrExists);
        }

        // Likewise, the presence of a value that shadows the target address is
        // disqualifying.
        match &dest {
            AddrVec::Conn(ConnAddr { ip: _, device: None }) | AddrVec::Listen(_) => {
                if socketmap.descendant_counts(&dest).len() != 0 {
                    return Err(InsertError::ShadowerExists);
                }
            }
            AddrVec::Conn(ConnAddr { ip: _, device: Some(_) }) => {
                // No need to check shadows here because there are no addresses
                // that shadow a ConnAddr with a device.
                debug_assert_eq!(socketmap.descendant_counts(&dest).len(), 0)
            }
        }

        // Listener addresses with devices present an extra complication: they
        // can conflict with entries at other addresses even though there's no
        // direct shadowing relationship. This can happen, for example, if a
        // connected socket with no device is present, and the target is the
        // same IP/port with a device specified.
        match dest {
            AddrVec::Listen(ListenerAddr { ip, device: Some(_device) }) => {
                let to_check = ListenerAddr { ip, device: None }.into();
                if socketmap.descendant_counts(&to_check).any(|(tag, _): &(_, NonZeroUsize)| {
                    match tag {
                        PosixAddrVecTag::Exclusive => true,
                        PosixAddrVecTag::ExclusiveDevice => false,
                    }
                }) {
                    return Err(InsertError::ShadowerExists);
                }
            }
            AddrVec::Listen(ListenerAddr { ip: _, device: None }) => (),
            AddrVec::Conn(_) => (),
        }
        Ok(())
    }

    fn try_get_dest<'a, 'b>(&'b mut self, _new_state: &'a St) -> Result<&'b mut Vec<I>, ()> {
        match self {
            PosixAddrState::Exclusive(_) | PosixAddrState::ExclusiveDevice(_) => Err(()),
        }
    }

    fn new_addr_state(_new_state: &St, addr: &A, id: I) -> Self {
        let device = match addr.clone().into() {
            AddrVec::Conn(ConnAddr { ip: _, device }) => device,
            AddrVec::Listen(ListenerAddr { ip: _, device }) => device,
        };
        match device {
            Some(_) => Self::ExclusiveDevice(id),
            None => Self::Exclusive(id),
        }
    }

    fn for_new_addr(self, new_addr: &A) -> Self {
        let new_device = match new_addr.clone().into() {
            AddrVec::Conn(ConnAddr { ip: _, device }) => device,
            AddrVec::Listen(ListenerAddr { ip: _, device }) => device,
        };
        match (self, new_device) {
            (PosixAddrState::Exclusive(s), Some(_))
            | (PosixAddrState::ExclusiveDevice(s), Some(_)) => PosixAddrState::ExclusiveDevice(s),
            (PosixAddrState::Exclusive(s), None) | (PosixAddrState::ExclusiveDevice(s), None) => {
                PosixAddrState::Exclusive(s)
            }
        }
    }

    fn remove_by_id(&mut self, _id: I) -> RemoveResult {
        match self {
            PosixAddrState::Exclusive(_) | PosixAddrState::ExclusiveDevice(_) => {
                RemoveResult::IsLast
            }
        }
    }
}
