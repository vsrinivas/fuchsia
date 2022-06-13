// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of POSIX-style socket conflict detection semantics on top
//! of [`SocketMapSpec`] that can be used to implement multiple types of
//! sockets.

use alloc::{vec, vec::Vec};
use core::{fmt::Debug, hash::Hash};
use todo_unused::todo_unused;

use derivative::Derivative;
use net_types::{ip::IpAddress, SpecifiedAddr};

use crate::{
    data_structures::socketmap::{IterShadows, SocketMap, Tagged},
    socket::{
        AddrVec, Bound, BoundSocketMap, IncompatibleError, InsertError, RemoveResult,
        SocketMapAddrStateSpec, SocketMapSpec,
    },
};

/// Describes the data types associated with types of network POSIX sockets.
///
/// Implementers of this trait get a free implementation of [`SocketMapSpec`]
/// with address vectors composed of the provided address and identifier types.
pub(crate) trait PosixSocketMapSpec: Sized {
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

    /// Checks whether a new socket with the provided sharing options can be
    /// inserted at the given address in the existing socket map, returning an
    /// error otherwise.
    fn check_posix_sharing(
        new_sharing: PosixSharingOptions,
        addr: AddrVec<Self>,
        socketmap: &SocketMap<AddrVec<Self>, Bound<Self>>,
    ) -> Result<(), InsertError>;
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
    // TODO(https://fxbug.dev/97822): Remove this when Bindings support for setting this is added.
    #[cfg_attr(not(test), allow(unused))]
    ReusePort(Vec<T>),
}

#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) enum PosixAddrType {
    AnyListener,
    SpecificListener,
    Connected,
}

impl<'a, P: PosixSocketMapSpec> From<&'a ListenerIpAddr<P>> for PosixAddrType {
    fn from(ListenerIpAddr { addr, identifier: _ }: &'a ListenerIpAddr<P>) -> Self {
        match addr {
            Some(_) => PosixAddrType::SpecificListener,
            None => PosixAddrType::AnyListener,
        }
    }
}

impl<'a, P: PosixSocketMapSpec> From<&'a ConnIpAddr<P>> for PosixAddrType {
    fn from(_: &'a ConnIpAddr<P>) -> Self {
        PosixAddrType::Connected
    }
}

#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) enum PosixSharingOptions {
    Exclusive,
    ReusePort,
}

impl PosixSharingOptions {
    pub(crate) fn is_shareable_with_new_state(&self, new_state: PosixSharingOptions) -> bool {
        match (self, new_state) {
            (PosixSharingOptions::Exclusive, PosixSharingOptions::Exclusive) => false,
            (PosixSharingOptions::Exclusive, PosixSharingOptions::ReusePort) => false,
            (PosixSharingOptions::ReusePort, PosixSharingOptions::Exclusive) => false,
            (PosixSharingOptions::ReusePort, PosixSharingOptions::ReusePort) => true,
        }
    }

    pub(crate) fn is_reuse_port(&self) -> bool {
        match self {
            PosixSharingOptions::Exclusive => false,
            PosixSharingOptions::ReusePort => true,
        }
    }
}

#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) struct PosixAddrVecTag {
    pub(crate) has_device: bool,
    pub(crate) addr_type: PosixAddrType,
    pub(crate) sharing: PosixSharingOptions,
}

impl<T, P: PosixSocketMapSpec> Tagged<ListenerAddr<P>> for PosixAddrState<T> {
    type Tag = PosixAddrVecTag;

    fn tag(&self, address: &ListenerAddr<P>) -> Self::Tag {
        let ListenerAddr { ip, device } = address;
        PosixAddrVecTag {
            has_device: device.is_some(),
            addr_type: ip.into(),
            sharing: self.to_sharing_options(),
        }
    }
}

impl<T, P: PosixSocketMapSpec> Tagged<ConnAddr<P>> for PosixAddrState<T> {
    type Tag = PosixAddrVecTag;

    fn tag(&self, address: &ConnAddr<P>) -> Self::Tag {
        let ConnAddr { ip, device } = address;
        PosixAddrVecTag {
            has_device: device.is_some(),
            addr_type: ip.into(),
            sharing: self.to_sharing_options(),
        }
    }
}

pub(crate) trait ToPosixSharingOptions {
    fn to_sharing_options(&self) -> PosixSharingOptions;
}

impl ToPosixSharingOptions for PosixAddrVecTag {
    fn to_sharing_options(&self) -> PosixSharingOptions {
        let PosixAddrVecTag { has_device: _, addr_type: _, sharing } = self;
        *sharing
    }
}

impl<T> ToPosixSharingOptions for PosixAddrState<T> {
    fn to_sharing_options(&self) -> PosixSharingOptions {
        match self {
            PosixAddrState::Exclusive(_) => PosixSharingOptions::Exclusive,
            PosixAddrState::ReusePort(_) => PosixSharingOptions::ReusePort,
        }
    }
}

impl<P: PosixSocketMapSpec> SocketMapSpec for P {
    type ListenerAddr = ListenerAddr<P>;

    type ConnAddr = ConnAddr<P>;

    type AddrVecTag = PosixAddrVecTag;

    type ListenerId = P::ListenerId;

    type ConnId = P::ConnId;

    // TODO(https://fxbug.dev/101914): Be generic about which part of the state
    // is not related to computing tags.
    type ListenerState = (P::ListenerState, PosixSharingOptions);

    type ConnState = (P::ConnState, PosixSharingOptions);

    type ListenerAddrState = PosixAddrState<P::ListenerId>;

    type ConnAddrState = PosixAddrState<P::ConnId>;
}

impl<T> ToPosixSharingOptions for (T, PosixSharingOptions) {
    fn to_sharing_options(&self) -> PosixSharingOptions {
        let (_state, sharing) = self;
        *sharing
    }
}

impl<A, St, I, P> SocketMapAddrStateSpec<A, St, I, P> for PosixAddrState<I>
where
    A: Into<AddrVec<P>> + Clone,
    P: PosixSocketMapSpec,
    St: ToPosixSharingOptions,
    I: PartialEq + Debug,
{
    fn check_for_conflicts(
        new_state: &St,
        addr: &A,
        socketmap: &SocketMap<AddrVec<P>, Bound<P>>,
    ) -> Result<(), InsertError> {
        P::check_posix_sharing(new_state.to_sharing_options(), addr.clone().into(), socketmap)
    }

    fn try_get_dest<'a, 'b>(
        &'b mut self,
        new_state: &'a St,
    ) -> Result<&'b mut Vec<I>, IncompatibleError> {
        match self {
            PosixAddrState::Exclusive(_) => Err(IncompatibleError),
            PosixAddrState::ReusePort(ids) => match new_state.to_sharing_options() {
                PosixSharingOptions::Exclusive => Err(IncompatibleError),
                PosixSharingOptions::ReusePort => Ok(ids),
            },
        }
    }

    fn new_addr_state(new_state: &St, id: I) -> Self {
        match new_state.to_sharing_options() {
            PosixSharingOptions::Exclusive => Self::Exclusive(id),
            PosixSharingOptions::ReusePort => Self::ReusePort(vec![id]),
        }
    }

    fn remove_by_id(&mut self, id: I) -> RemoveResult {
        match self {
            PosixAddrState::Exclusive(_) => RemoveResult::IsLast,
            PosixAddrState::ReusePort(ids) => {
                let index = ids.iter().position(|i| i == &id).expect("couldn't find ID to remove");
                assert_eq!(ids.swap_remove(index), id);
                if ids.is_empty() {
                    RemoveResult::IsLast
                } else {
                    RemoveResult::Success
                }
            }
        }
    }
}

impl<P: PosixSocketMapSpec> BoundSocketMap<P> {
    pub(crate) fn try_insert_listener_with_sharing(
        &mut self,
        listener_addr: ListenerAddr<P>,
        state: P::ListenerState,
        sharing: PosixSharingOptions,
    ) -> Result<P::ListenerId, (InsertError, P::ListenerState)> {
        self.try_insert_listener(listener_addr, (state, sharing))
            .map_err(|(e, (state, _sharing)): (_, (_, PosixSharingOptions))| (e, state))
    }

    pub(crate) fn try_insert_conn_with_sharing(
        &mut self,
        conn_addr: ConnAddr<P>,
        state: P::ConnState,
        sharing: PosixSharingOptions,
    ) -> Result<P::ConnId, (InsertError, P::ConnState)> {
        self.try_insert_conn(conn_addr, (state, sharing))
            .map_err(|(e, (state, _)): (_, (_, PosixSharingOptions))| (e, state))
    }

    #[todo_unused("https://fxbug.dev/95688")]
    pub(crate) fn get_listener_by_id_mut(
        &mut self,
        id: P::ListenerId,
    ) -> Option<(&mut P::ListenerState, PosixSharingOptions, &ListenerAddr<P>)> {
        let Self { listener_id_to_sock, conn_id_to_sock: _, addr_to_state: _ } = self;
        listener_id_to_sock
            .get_mut(id.clone().into())
            .map(|((state, sharing), addr)| (state, *sharing, &*addr))
    }

    #[todo_unused("https://fxbug.dev/95688")]
    pub(crate) fn get_conn_by_id_mut(
        &mut self,
        id: P::ConnId,
    ) -> Option<(&mut P::ConnState, PosixSharingOptions, &ConnAddr<P>)> {
        let Self { listener_id_to_sock: _, conn_id_to_sock, addr_to_state: _ } = self;
        conn_id_to_sock
            .get_mut(id.clone().into())
            .map(|((state, sharing), addr)| (state, *sharing, &*addr))
    }
}

#[cfg(test)]
mod tests {
    use core::{convert::Infallible as Never, num::NonZeroU16};

    use assert_matches::assert_matches;
    use itertools::Itertools as _;
    use net_declare::net_ip_v4 as ip_v4;
    use net_types::ip::{Ip, IpVersionMarker, Ipv4};
    use test_case::test_case;

    use super::*;
    use crate::{
        ip::DummyDeviceId,
        socket::{BoundSocketMap, InsertError},
    };

    struct TransportSocketPosixSpec<I: Ip> {
        _ip: IpVersionMarker<I>,
        _never: Never,
    }

    impl ToPosixSharingOptions for PosixSharingOptions {
        fn to_sharing_options(&self) -> PosixSharingOptions {
            *self
        }
    }

    #[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
    struct ListenerId(usize);

    #[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
    struct ConnId(usize);

    impl From<usize> for ListenerId {
        fn from(id: usize) -> Self {
            Self(id)
        }
    }
    impl From<ListenerId> for usize {
        fn from(ListenerId(id): ListenerId) -> Self {
            id
        }
    }
    impl From<usize> for ConnId {
        fn from(id: usize) -> Self {
            Self(id)
        }
    }
    impl From<ConnId> for usize {
        fn from(ConnId(id): ConnId) -> Self {
            id
        }
    }

    impl<I: Ip> PosixSocketMapSpec for TransportSocketPosixSpec<I> {
        type IpAddress = I::Addr;
        type RemoteAddr = (SpecifiedAddr<I::Addr>, NonZeroU16);
        type LocalIdentifier = NonZeroU16;
        type DeviceId = DummyDeviceId;
        type ListenerId = ListenerId;
        type ConnId = ConnId;
        type ListenerState = ();
        type ConnState = ();

        fn check_posix_sharing(
            new_sharing: PosixSharingOptions,
            addr: AddrVec<Self>,
            socketmap: &SocketMap<AddrVec<Self>, Bound<Self>>,
        ) -> Result<(), InsertError> {
            crate::transport::udp::check_posix_sharing(new_sharing, addr, socketmap)
        }
    }

    fn listen<I: Ip>(ip: I::Addr, port: u16) -> AddrVec<TransportSocketPosixSpec<I>> {
        let addr = SpecifiedAddr::new(ip);
        let port = NonZeroU16::new(port).expect("port must be nonzero");
        AddrVec::Listen(ListenerAddr {
            ip: ListenerIpAddr { addr, identifier: port },
            device: None,
        })
    }

    fn listen_device<I: Ip>(
        ip: I::Addr,
        port: u16,
        device: DummyDeviceId,
    ) -> AddrVec<TransportSocketPosixSpec<I>> {
        let addr = SpecifiedAddr::new(ip);
        let port = NonZeroU16::new(port).expect("port must be nonzero");
        AddrVec::Listen(ListenerAddr {
            ip: ListenerIpAddr { addr, identifier: port },
            device: Some(device),
        })
    }

    fn conn<I: Ip>(
        local_ip: I::Addr,
        local_port: u16,
        remote_ip: I::Addr,
        remote_port: u16,
    ) -> AddrVec<TransportSocketPosixSpec<I>> {
        let local_ip = SpecifiedAddr::new(local_ip).expect("addr must be specified");
        let local_port = NonZeroU16::new(local_port).expect("port must be nonzero");
        let remote_ip = SpecifiedAddr::new(remote_ip).expect("addr must be specified");
        let remote_port = NonZeroU16::new(remote_port).expect("port must be nonzero");
        AddrVec::Conn(ConnAddr {
            ip: ConnIpAddr {
                local_ip,
                local_identifier: local_port,
                remote: (remote_ip, remote_port),
            },
            device: None,
        })
    }

    #[test_case([
        (listen(ip_v4!("0.0.0.0"), 1), PosixSharingOptions::Exclusive),
        (listen(ip_v4!("0.0.0.0"), 2), PosixSharingOptions::Exclusive)],
            Ok(()); "listen_any_ip_different_port")]
    #[test_case([
        (listen(ip_v4!("0.0.0.0"), 1), PosixSharingOptions::Exclusive),
        (listen(ip_v4!("0.0.0.0"), 1), PosixSharingOptions::Exclusive)],
            Err(InsertError::Exists); "any_ip_same_port")]
    #[test_case([
        (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::Exclusive),
        (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::Exclusive)],
            Err(InsertError::Exists); "listen_same_specific_ip")]
    #[test_case([
        (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::ReusePort),
        (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::ReusePort)],
            Ok(()); "listen_same_specific_ip_reuse_port")]
    #[test_case([
        (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::Exclusive),
        (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::ReusePort)],
            Err(InsertError::Exists); "listen_same_specific_ip_exclusive_reuse_port")]
    #[test_case([
        (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::ReusePort),
        (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::Exclusive)],
            Err(InsertError::Exists); "listen_same_specific_ip_reuse_port_exclusive")]
    #[test_case([
        (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::ReusePort),
        (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 2), PosixSharingOptions::ReusePort)],
            Ok(()); "conn_shadows_listener_reuse_port")]
    #[test_case([
        (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::Exclusive),
        (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 2), PosixSharingOptions::Exclusive)],
            Err(InsertError::ShadowAddrExists); "conn_shadows_listener_exclusive")]
    #[test_case([
        (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::Exclusive),
        (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 2), PosixSharingOptions::ReusePort)],
            Err(InsertError::ShadowAddrExists); "conn_shadows_listener_exclusive_reuse_port")]
    #[test_case([
        (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::ReusePort),
        (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 2), PosixSharingOptions::Exclusive)],
            Err(InsertError::ShadowAddrExists); "conn_shadows_listener_reuse_port_exclusive")]
    #[test_case([
        (listen_device(ip_v4!("1.1.1.1"), 1, DummyDeviceId), PosixSharingOptions::Exclusive),
        (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 2), PosixSharingOptions::Exclusive)],
            Err(InsertError::IndirectConflict); "conn_indirect_conflict_specific_listener")]
    #[test_case([
        (listen_device(ip_v4!("0.0.0.0"), 1, DummyDeviceId), PosixSharingOptions::Exclusive),
        (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 2), PosixSharingOptions::Exclusive)],
            Err(InsertError::IndirectConflict); "conn_indirect_conflict_any_listener")]
    #[test_case([
        (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 2), PosixSharingOptions::Exclusive),
        (listen_device(ip_v4!("1.1.1.1"), 1, DummyDeviceId), PosixSharingOptions::Exclusive)],
            Err(InsertError::IndirectConflict); "specific_listener_indirect_conflict_conn")]
    #[test_case([
        (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 2), PosixSharingOptions::Exclusive),
        (listen_device(ip_v4!("0.0.0.0"), 1, DummyDeviceId), PosixSharingOptions::Exclusive)],
            Err(InsertError::IndirectConflict); "any_listener_indirect_conflict_conn")]
    fn bind_sequence<
        C: IntoIterator<Item = (AddrVec<TransportSocketPosixSpec<Ipv4>>, PosixSharingOptions)>,
    >(
        spec: C,
        expected: Result<(), InsertError>,
    ) {
        let mut map = BoundSocketMap::<TransportSocketPosixSpec<Ipv4>>::default();
        let mut spec = spec.into_iter().peekable();
        let mut try_insert = |(addr, options)| {
            match addr {
                AddrVec::Conn(c) => map.try_insert_conn_with_sharing(c, (), options).map(|_| ()),
                AddrVec::Listen(l) => {
                    map.try_insert_listener_with_sharing(l, (), options).map(|_| ())
                }
            }
            .map_err(|(e, ())| e)
        };
        let last = loop {
            let one_spec = spec.next().expect("empty list of test cases");
            if spec.peek().is_none() {
                break one_spec;
            } else {
                try_insert(one_spec).expect("intermediate bind failed")
            }
        };

        let result = try_insert(last);
        assert_eq!(result, expected);
    }

    #[test_case([
            (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::Exclusive),
            (listen(ip_v4!("2.2.2.2"), 2), PosixSharingOptions::Exclusive),
        ]; "distinct")]
    #[test_case([
            (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::ReusePort),
            (listen(ip_v4!("1.1.1.1"), 1), PosixSharingOptions::ReusePort),
        ]; "listen_reuse_port")]
    #[test_case([
            (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 3), PosixSharingOptions::ReusePort),
            (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 3), PosixSharingOptions::ReusePort),
        ]; "conn_reuse_port")]
    fn remove_sequence<I>(spec: I)
    where
        I: IntoIterator<Item = (AddrVec<TransportSocketPosixSpec<Ipv4>>, PosixSharingOptions)>,
        I::IntoIter: ExactSizeIterator,
    {
        enum Socket<T: PosixSocketMapSpec> {
            Listener(ListenerId, ListenerAddr<T>),
            Conn(ConnId, ConnAddr<T>),
        }
        let spec = spec.into_iter();
        let spec_len = spec.len();
        for spec in spec.permutations(spec_len) {
            let mut map = BoundSocketMap::<TransportSocketPosixSpec<Ipv4>>::default();
            let sockets = spec
                .into_iter()
                .map(|(addr, options)| {
                    match addr {
                        AddrVec::Conn(c) => map
                            .try_insert_conn_with_sharing(c.clone(), (), options)
                            .map(|id| Socket::Conn(id, c)),
                        AddrVec::Listen(l) => map
                            .try_insert_listener_with_sharing(l.clone(), (), options)
                            .map(|id| Socket::Listener(id, l)),
                    }
                    .expect("insert_failed")
                })
                .collect::<Vec<_>>();

            for socket in sockets {
                match socket {
                    Socket::Listener(l, addr) => {
                        assert_matches!(map.remove_listener_by_id(l),
                                        Some((_, a)) => assert_eq!(a, addr));
                    }
                    Socket::Conn(c, addr) => {
                        assert_matches!(map.remove_conn_by_id(c),
                                        Some((_, a)) => assert_eq!(a, addr));
                    }
                }
            }
        }
    }

    #[test]
    fn socketmap_mut_ref() {
        let mut map = BoundSocketMap::<TransportSocketPosixSpec<Ipv4>>::default();
        let port = NonZeroU16::new(42).unwrap();
        let listener_addr = ListenerAddr {
            ip: ListenerIpAddr { addr: SpecifiedAddr::new(ip_v4!("1.1.1.1")), identifier: port },
            device: None,
        };
        let listener_id = map
            .try_insert_listener_with_sharing(
                listener_addr.clone(),
                (),
                PosixSharingOptions::Exclusive,
            )
            .expect("failed to insert");
        let (&mut (), sharing, addr) =
            map.get_listener_by_id_mut(listener_id).expect("failed to get listener");
        assert_eq!(sharing, PosixSharingOptions::Exclusive);
        assert_eq!(addr, &listener_addr);
        let conn_addr = ConnAddr {
            ip: ConnIpAddr {
                local_ip: SpecifiedAddr::new(ip_v4!("2.2.2.2")).unwrap(),
                local_identifier: port,
                remote: (SpecifiedAddr::new(ip_v4!("3.3.3.3")).unwrap(), port),
            },
            device: None,
        };
        let conn_id = map
            .try_insert_conn_with_sharing(conn_addr.clone(), (), PosixSharingOptions::Exclusive)
            .expect("failed to insert");
        let (&mut (), sharing, addr) = map.get_conn_by_id_mut(conn_id).expect("failed to get conn");
        assert_eq!(sharing, PosixSharingOptions::Exclusive);
        assert_eq!(addr, &conn_addr);
    }
}
