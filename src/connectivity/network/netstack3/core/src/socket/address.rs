// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A collection of types that represent the various parts of socket addresses.

use core::{convert::Infallible as Never, marker::PhantomData, num::NonZeroU16};

use derivative::Derivative;
use net_types::{
    ip::{Ip, IpAddress},
    SpecifiedAddr,
};

use crate::{
    ip::{IpDeviceId, IpExt},
    socket::{AddrVec, SocketMapAddrSpec},
};

/// The IP address and identifier (port) of a listening socket.
#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) struct ListenerIpAddr<A: IpAddress, LI> {
    /// The specific address being listened on, or `None` for all addresses.
    pub(crate) addr: Option<SpecifiedAddr<A>>,
    /// The local identifier (i.e. port for TCP/UDP).
    pub(crate) identifier: LI,
}

/// The address of a listening socket.
#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) struct ListenerAddr<A: IpAddress, D, P> {
    pub(crate) ip: ListenerIpAddr<A, P>,
    pub(crate) device: Option<D>,
}

// The IP address and identifier (port) of a connected socket.
#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) struct ConnIpAddr<A: IpAddress, LI, RI> {
    pub(crate) local: (SpecifiedAddr<A>, LI),
    pub(crate) remote: (SpecifiedAddr<A>, RI),
}

/// The address of a connected socket.
#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) struct ConnAddr<A: IpAddress, D, LI, RI> {
    pub(crate) ip: ConnIpAddr<A, LI, RI>,
    pub(crate) device: Option<D>,
}

/// Uninstantiable type used to implement [`SocketMapAddrSpec`] for addresses
/// with IP addresses and 16-bit local and remote port identifiers.
pub(crate) struct IpPortSpec<I, D>(PhantomData<(I, D)>, Never);

impl<I: Ip + IpExt, D: IpDeviceId> SocketMapAddrSpec for IpPortSpec<I, D> {
    type IpVersion = I;
    type IpAddr = I::Addr;
    type DeviceId = D;
    type RemoteIdentifier = NonZeroU16;
    type LocalIdentifier = NonZeroU16;
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
pub(crate) enum IpAddrVec<A: SocketMapAddrSpec> {
    Listener(ListenerIpAddr<A::IpAddr, A::LocalIdentifier>),
    Connected(ConnIpAddr<A::IpAddr, A::LocalIdentifier, A::RemoteIdentifier>),
}

impl<A: SocketMapAddrSpec> IpAddrVec<A> {
    fn with_device(self, device: Option<A::DeviceId>) -> AddrVec<A> {
        match self {
            IpAddrVec::Listener(ip) => AddrVec::Listen(ListenerAddr { ip, device }),
            IpAddrVec::Connected(ip) => AddrVec::Conn(ConnAddr { ip, device }),
        }
    }
}

impl<A: SocketMapAddrSpec> IpAddrVec<A> {
    /// Returns the next smallest address vector that would receive all the same
    /// packets as this one.
    ///
    /// Address vectors are ordered by their shadowing relationship, such that
    /// a "smaller" vector shadows a "larger" one. This function returns the
    /// smallest of the set of shadows of `self`.
    fn widen(self) -> Option<Self> {
        match self {
            IpAddrVec::Listener(ListenerIpAddr { addr: None, identifier }) => {
                let _: A::LocalIdentifier = identifier;
                None
            }
            IpAddrVec::Connected(ConnIpAddr { local: (local_ip, local_identifier), remote }) => {
                let _: (SpecifiedAddr<A::IpAddr>, A::RemoteIdentifier) = remote;
                Some(ListenerIpAddr { addr: Some(local_ip), identifier: local_identifier })
            }
            IpAddrVec::Listener(ListenerIpAddr { addr: Some(addr), identifier }) => {
                let _: SpecifiedAddr<A::IpAddr> = addr;
                Some(ListenerIpAddr { addr: None, identifier })
            }
        }
        .map(IpAddrVec::Listener)
    }
}

enum AddrVecIterInner<A: SocketMapAddrSpec> {
    WithDevice { device: A::DeviceId, emitted_device: bool, addr: IpAddrVec<A> },
    NoDevice { addr: IpAddrVec<A> },
    Done,
}

impl<A: SocketMapAddrSpec> Iterator for AddrVecIterInner<A> {
    type Item = AddrVec<A>;

    fn next(&mut self) -> Option<Self::Item> {
        match self {
            Self::Done => None,
            Self::WithDevice { device, emitted_device, addr } => {
                if !*emitted_device {
                    *emitted_device = true;
                    Some(addr.clone().with_device(Some(device.clone())))
                } else {
                    let r = addr.clone().with_device(None);
                    if let Some(next) = addr.clone().widen() {
                        *addr = next;
                        *emitted_device = false;
                    } else {
                        *self = Self::Done;
                    }
                    Some(r)
                }
            }
            Self::NoDevice { addr } => {
                let r = addr.clone().with_device(None);
                if let Some(next) = addr.clone().widen() {
                    *addr = next;
                } else {
                    *self = Self::Done
                }
                Some(r)
            }
        }
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
pub(crate) struct AddrVecIter<A: SocketMapAddrSpec>(AddrVecIterInner<A>);

impl<A: SocketMapAddrSpec> AddrVecIter<A> {
    pub(crate) fn with_device(addr: IpAddrVec<A>, device: A::DeviceId) -> Self {
        Self(AddrVecIterInner::WithDevice { device, emitted_device: false, addr })
    }

    pub(crate) fn without_device(addr: IpAddrVec<A>) -> Self {
        Self(AddrVecIterInner::NoDevice { addr })
    }
}

impl<A: SocketMapAddrSpec> Iterator for AddrVecIter<A> {
    type Item = AddrVec<A>;

    fn next(&mut self) -> Option<Self::Item> {
        let Self(it) = self;
        it.next()
    }
}
