// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of POSIX-style socket conflict detection semantics on top
//! of [`SocketMapSpec`] that can be used to implement multiple types of
//! sockets.

use alloc::{vec, vec::Vec};
use core::{fmt::Debug, hash::Hash};

use net_types::ip::IpAddress;

use crate::{
    data_structures::socketmap::{IterShadows, SocketMap, Tagged},
    socket::{
        address::{AddrVecIter, ConnAddr, ConnIpAddr, IpAddrVec, ListenerAddr, ListenerIpAddr},
        AddrVec, Bound, IncompatibleError, InsertError, RemoveResult, SocketAddrType,
        SocketMapAddrSpec, SocketMapAddrStateSpec, SocketMapConflictPolicy, SocketMapStateSpec,
    },
};

/// Describes the data types associated with types of network POSIX sockets.
///
/// Implementers of this trait get a free implementation of
/// [`SocketMapStateSpec`].
pub(crate) trait PosixSocketStateSpec: Sized {
    /// An identifier for a listening socket.
    type ListenerId: Clone + Into<usize> + From<usize> + Debug + Eq;
    /// An identifier for a connected socket.
    type ConnId: Clone + Into<usize> + From<usize> + Debug + Eq;

    /// The state for a listening socket.
    type ListenerState: Debug;
    /// The state for a connected socket.
    type ConnState: Debug;
}

pub(crate) trait PosixConflictPolicy<A: SocketMapAddrSpec>: PosixSocketStateSpec {
    /// Checks whether a new socket with the provided sharing options can be
    /// inserted at the given address in the existing socket map, returning an
    /// error otherwise.
    fn check_posix_sharing(
        new_sharing: PosixSharingOptions,
        addr: AddrVec<A>,
        socketmap: &SocketMap<AddrVec<A>, Bound<Self>>,
    ) -> Result<(), InsertError>;
}

impl<A: SocketMapAddrSpec> From<ListenerIpAddr<A::IpAddr, A::LocalIdentifier>> for IpAddrVec<A> {
    fn from(listener: ListenerIpAddr<A::IpAddr, A::LocalIdentifier>) -> Self {
        IpAddrVec::Listener(listener)
    }
}

impl<A: SocketMapAddrSpec> From<ConnIpAddr<A::IpAddr, A::LocalIdentifier, A::RemoteIdentifier>>
    for IpAddrVec<A>
{
    fn from(conn: ConnIpAddr<A::IpAddr, A::LocalIdentifier, A::RemoteIdentifier>) -> Self {
        IpAddrVec::Connected(conn)
    }
}

impl<A: SocketMapAddrSpec> From<ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>>
    for AddrVec<A>
{
    fn from(listener: ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>) -> Self {
        AddrVec::Listen(listener)
    }
}

impl<A: SocketMapAddrSpec>
    From<ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>> for AddrVec<A>
{
    fn from(
        conn: ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
    ) -> Self {
        AddrVec::Conn(conn)
    }
}
pub(crate) struct PosixAddrVecIter<A: SocketMapAddrSpec>(AddrVecIter<A>);

impl<A: SocketMapAddrSpec> Iterator for PosixAddrVecIter<A> {
    type Item = AddrVec<A>;

    fn next(&mut self) -> Option<Self::Item> {
        let Self(it) = self;
        it.next()
    }
}

impl<A: SocketMapAddrSpec> PosixAddrVecIter<A> {
    pub(crate) fn with_device(addr: impl Into<IpAddrVec<A>>, device: A::DeviceId) -> Self {
        Self(AddrVecIter::with_device(addr.into(), device))
    }
}

impl<A: SocketMapAddrSpec> IterShadows for AddrVec<A> {
    type IterShadows = PosixAddrVecIter<A>;

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

#[derive(Debug, Eq, PartialEq)]
pub(crate) enum PosixAddrState<T> {
    Exclusive(T),
    // TODO(https://fxbug.dev/97822): Remove this when Bindings support for setting this is added.
    #[cfg_attr(not(test), allow(unused))]
    ReusePort(Vec<T>),
}

impl<'a, A: IpAddress, LI> From<&'a ListenerIpAddr<A, LI>> for SocketAddrType {
    fn from(ListenerIpAddr { addr, identifier: _ }: &'a ListenerIpAddr<A, LI>) -> Self {
        match addr {
            Some(_) => SocketAddrType::SpecificListener,
            None => SocketAddrType::AnyListener,
        }
    }
}

impl<'a, A: IpAddress, LI, RI> From<&'a ConnIpAddr<A, LI, RI>> for SocketAddrType {
    fn from(_: &'a ConnIpAddr<A, LI, RI>) -> Self {
        SocketAddrType::Connected
    }
}

#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub(crate) enum PosixSharingOptions {
    Exclusive,
    ReusePort,
}

impl Default for PosixSharingOptions {
    fn default() -> Self {
        Self::Exclusive
    }
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
    pub(crate) addr_type: SocketAddrType,
    pub(crate) sharing: PosixSharingOptions,
}

impl<T, A: IpAddress, D, LI> Tagged<ListenerAddr<A, D, LI>> for PosixAddrState<T> {
    type Tag = PosixAddrVecTag;

    fn tag(&self, address: &ListenerAddr<A, D, LI>) -> Self::Tag {
        let ListenerAddr { ip, device } = address;
        PosixAddrVecTag {
            has_device: device.is_some(),
            addr_type: ip.into(),
            sharing: self.to_sharing_options(),
        }
    }
}

impl<T, A: IpAddress, D, LI, RI> Tagged<ConnAddr<A, D, LI, RI>> for PosixAddrState<T> {
    type Tag = PosixAddrVecTag;

    fn tag(&self, address: &ConnAddr<A, D, LI, RI>) -> Self::Tag {
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

impl<P: PosixSocketStateSpec> SocketMapStateSpec for P {
    type AddrVecTag = PosixAddrVecTag;

    type ListenerId = P::ListenerId;

    type ConnId = P::ConnId;

    type ListenerState = P::ListenerState;
    type ListenerSharingState = PosixSharingOptions;

    type ConnState = P::ConnState;
    type ConnSharingState = PosixSharingOptions;

    type ListenerAddrState = PosixAddrState<P::ListenerId>;

    type ConnAddrState = PosixAddrState<P::ConnId>;
}

impl<T> ToPosixSharingOptions for (T, PosixSharingOptions) {
    fn to_sharing_options(&self) -> PosixSharingOptions {
        let (_state, sharing) = self;
        *sharing
    }
}

impl<I: Debug + Eq> SocketMapAddrStateSpec for PosixAddrState<I> {
    type Id = I;
    type SharingState = PosixSharingOptions;
    fn new(new_sharing_state: &PosixSharingOptions, id: I) -> Self {
        match new_sharing_state {
            PosixSharingOptions::Exclusive => Self::Exclusive(id),
            PosixSharingOptions::ReusePort => Self::ReusePort(vec![id]),
        }
    }

    fn try_get_dest<'a, 'b>(
        &'b mut self,
        new_sharing_state: &'a PosixSharingOptions,
    ) -> Result<&'b mut Vec<I>, IncompatibleError> {
        match self {
            PosixAddrState::Exclusive(_) => Err(IncompatibleError),
            PosixAddrState::ReusePort(ids) => match new_sharing_state {
                PosixSharingOptions::Exclusive => Err(IncompatibleError),
                PosixSharingOptions::ReusePort => Ok(ids),
            },
        }
    }

    fn could_insert(
        &self,
        new_sharing_state: &Self::SharingState,
    ) -> Result<(), IncompatibleError> {
        match self {
            PosixAddrState::Exclusive(_) => Err(IncompatibleError),
            PosixAddrState::ReusePort(_) => match new_sharing_state {
                PosixSharingOptions::Exclusive => Err(IncompatibleError),
                PosixSharingOptions::ReusePort => Ok(()),
            },
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

impl<AA, P, A> SocketMapConflictPolicy<AA, PosixSharingOptions, A> for P
where
    AA: Into<AddrVec<A>> + Clone,
    P: PosixSocketStateSpec + PosixConflictPolicy<A>,
    A: SocketMapAddrSpec,
{
    fn check_for_conflicts(
        new_sharing_state: &PosixSharingOptions,
        addr: &AA,
        socketmap: &SocketMap<AddrVec<A>, Bound<P>>,
    ) -> Result<(), InsertError> {
        P::check_posix_sharing(*new_sharing_state, addr.clone().into(), socketmap)
    }
}

#[cfg(test)]
mod tests {
    use core::{convert::Infallible as Never, num::NonZeroU16};

    use assert_matches::assert_matches;
    use itertools::Itertools as _;
    use net_declare::net_ip_v4 as ip_v4;
    use net_types::{
        ip::{Ip, IpVersionMarker, Ipv4},
        SpecifiedAddr,
    };
    use test_case::test_case;

    use super::*;
    use crate::{
        ip::{testutil::FakeDeviceId, IpExt},
        socket::{BoundSocketMap, InsertError, SocketTypeStateEntry as _, SocketTypeStateMut as _},
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

    impl<I: Ip + IpExt> SocketMapAddrSpec for TransportSocketPosixSpec<I> {
        type IpVersion = I;
        type IpAddr = I::Addr;
        type RemoteIdentifier = NonZeroU16;
        type LocalIdentifier = NonZeroU16;
        type DeviceId = FakeDeviceId;
    }

    impl<I: Ip> PosixSocketStateSpec for TransportSocketPosixSpec<I> {
        type ListenerId = ListenerId;
        type ConnId = ConnId;
        type ListenerState = ();
        type ConnState = ();
    }

    type FakeBoundSocketMap<I> =
        BoundSocketMap<TransportSocketPosixSpec<I>, TransportSocketPosixSpec<I>>;

    impl<I: Ip + IpExt> PosixConflictPolicy<TransportSocketPosixSpec<I>>
        for TransportSocketPosixSpec<I>
    {
        fn check_posix_sharing(
            new_sharing: PosixSharingOptions,
            addr: AddrVec<TransportSocketPosixSpec<I>>,
            socketmap: &SocketMap<AddrVec<TransportSocketPosixSpec<I>>, Bound<Self>>,
        ) -> Result<(), InsertError> {
            crate::transport::udp::check_posix_sharing(new_sharing, addr, socketmap)
        }
    }

    fn listen<I: Ip + IpExt>(ip: I::Addr, port: u16) -> AddrVec<TransportSocketPosixSpec<I>> {
        let addr = SpecifiedAddr::new(ip);
        let port = NonZeroU16::new(port).expect("port must be nonzero");
        AddrVec::Listen(ListenerAddr {
            ip: ListenerIpAddr { addr, identifier: port },
            device: None,
        })
    }

    fn listen_device<I: Ip + IpExt>(
        ip: I::Addr,
        port: u16,
        device: FakeDeviceId,
    ) -> AddrVec<TransportSocketPosixSpec<I>> {
        let addr = SpecifiedAddr::new(ip);
        let port = NonZeroU16::new(port).expect("port must be nonzero");
        AddrVec::Listen(ListenerAddr {
            ip: ListenerIpAddr { addr, identifier: port },
            device: Some(device),
        })
    }

    fn conn<I: Ip + IpExt>(
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
            ip: ConnIpAddr { local: (local_ip, local_port), remote: (remote_ip, remote_port) },
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
        (listen_device(ip_v4!("1.1.1.1"), 1, FakeDeviceId), PosixSharingOptions::Exclusive),
        (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 2), PosixSharingOptions::Exclusive)],
            Err(InsertError::IndirectConflict); "conn_indirect_conflict_specific_listener")]
    #[test_case([
        (listen_device(ip_v4!("0.0.0.0"), 1, FakeDeviceId), PosixSharingOptions::Exclusive),
        (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 2), PosixSharingOptions::Exclusive)],
            Err(InsertError::IndirectConflict); "conn_indirect_conflict_any_listener")]
    #[test_case([
        (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 2), PosixSharingOptions::Exclusive),
        (listen_device(ip_v4!("1.1.1.1"), 1, FakeDeviceId), PosixSharingOptions::Exclusive)],
            Err(InsertError::IndirectConflict); "specific_listener_indirect_conflict_conn")]
    #[test_case([
        (conn(ip_v4!("1.1.1.1"), 1, ip_v4!("2.2.2.2"), 2), PosixSharingOptions::Exclusive),
        (listen_device(ip_v4!("0.0.0.0"), 1, FakeDeviceId), PosixSharingOptions::Exclusive)],
            Err(InsertError::IndirectConflict); "any_listener_indirect_conflict_conn")]
    fn bind_sequence<
        C: IntoIterator<Item = (AddrVec<TransportSocketPosixSpec<Ipv4>>, PosixSharingOptions)>,
    >(
        spec: C,
        expected: Result<(), InsertError>,
    ) {
        let mut map = FakeBoundSocketMap::<Ipv4>::default();
        let mut spec = spec.into_iter().peekable();
        let mut try_insert = |(addr, options)| {
            match addr {
                AddrVec::Conn(c) => map.conns_mut().try_insert(c, (), options).map(|_| ()),
                AddrVec::Listen(l) => map.listeners_mut().try_insert(l, (), options).map(|_| ()),
            }
            .map_err(|(e, (), _)| e)
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
        enum Socket<A: IpAddress, D, LI, RI> {
            Listener(ListenerId, ListenerAddr<A, D, LI>),
            Conn(ConnId, ConnAddr<A, D, LI, RI>),
        }
        let spec = spec.into_iter();
        let spec_len = spec.len();
        for spec in spec.permutations(spec_len) {
            let mut map = FakeBoundSocketMap::<Ipv4>::default();
            let sockets = spec
                .into_iter()
                .map(|(addr, options)| {
                    match addr {
                        AddrVec::Conn(c) => map
                            .conns_mut()
                            .try_insert(c.clone(), (), options)
                            .map(|entry| Socket::Conn(entry.id(), c)),
                        AddrVec::Listen(l) => map
                            .listeners_mut()
                            .try_insert(l.clone(), (), options)
                            .map(|entry| Socket::Listener(entry.id(), l)),
                    }
                    .expect("insert_failed")
                })
                .collect::<Vec<_>>();

            for socket in sockets {
                match socket {
                    Socket::Listener(l, addr) => {
                        assert_matches!(map.listeners_mut().remove(&l),
                                        Some((_, _, a)) => assert_eq!(a, addr));
                    }
                    Socket::Conn(c, addr) => {
                        assert_matches!(map.conns_mut().remove(&c),
                                        Some((_, _, a)) => assert_eq!(a, addr));
                    }
                }
            }
        }
    }
}
