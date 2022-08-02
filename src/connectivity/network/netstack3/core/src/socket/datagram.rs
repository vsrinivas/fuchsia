// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Shared code for implementing datagram sockets.

use alloc::collections::HashSet;
use core::{
    hash::Hash,
    num::{NonZeroU16, NonZeroUsize},
    ops::RangeInclusive,
};
use nonzero_ext::nonzero;
use rand::RngCore;

use derivative::Derivative;
use net_types::{
    ip::{Ip, IpAddress},
    MulticastAddr,
};

use crate::{
    algorithm::{PortAlloc, PortAllocImpl, ProtocolFlowId},
    data_structures::{
        socketmap::{IterShadows as _, Tagged},
        IdMap,
    },
    ip::{IpDeviceId, IpExt},
    socket::{
        address::{ConnAddr, ConnIpAddr, IpPortSpec},
        posix::PosixSharingOptions,
        AddrVec, Bound, SocketTypeState as _, {BoundSocketMap, SocketMapStateSpec},
    },
};

type DatagramBoundSocketMap<I, D, S> = BoundSocketMap<IpPortSpec<I, D>, S>;

/// Datagram socket storage.
#[derive(Derivative)]
#[derivative(Default(bound = ""))]
pub(crate) struct DatagramSockets<
    I: IpExt,
    D: IpDeviceId,
    S: SocketMapStateSpec + DatagramPortAlloc,
> where
    Bound<S>: Tagged<AddrVec<IpPortSpec<I, D>>>,
{
    pub(crate) bound: DatagramBoundSocketMap<I, D, S>,
    pub(crate) unbound: IdMap<UnboundSocketState<I, D>>,
    /// lazy_port_alloc is lazy-initialized when it's used.
    pub(crate) lazy_port_alloc: Option<PortAlloc<DatagramBoundSocketMap<I, D, S>>>,
}

#[derive(Debug, Derivative)]
pub(crate) struct UnboundSocketState<I: Ip, D> {
    pub(crate) device: Option<D>,
    pub(crate) sharing: PosixSharingOptions,
    pub(crate) multicast_memberships: MulticastMemberships<I::Addr, D>,
}

pub(crate) trait DatagramPortAlloc {
    const EPHEMERAL_RANGE: RangeInclusive<u16>;
}

impl<I: Ip + IpExt, D: IpDeviceId, S: SocketMapStateSpec + DatagramPortAlloc> PortAllocImpl
    for DatagramBoundSocketMap<I, D, S>
where
    Bound<S>: Tagged<AddrVec<IpPortSpec<I, D>>>,
{
    const TABLE_SIZE: NonZeroUsize = nonzero!(20usize);
    const EPHEMERAL_RANGE: RangeInclusive<u16> = S::EPHEMERAL_RANGE;
    type Id = ProtocolFlowId<I::Addr>;

    fn is_port_available(&self, id: &Self::Id, port: u16) -> bool {
        // We can safely unwrap here, because the ports received in
        // `is_port_available` are guaranteed to be in `EPHEMERAL_RANGE`.
        let port = NonZeroU16::new(port).unwrap();
        let conn = ConnAddr::from_protocol_flow_and_local_port(id, port);

        // A port is free if there are no sockets currently using it, and if
        // there are no sockets that are shadowing it.
        AddrVec::from(conn).iter_shadows().all(|a| match &a {
            AddrVec::Listen(l) => self.listeners().get_by_addr(&l).is_none(),
            AddrVec::Conn(c) => self.conns().get_by_addr(&c).is_none(),
        } && self.get_shadower_counts(&a) == 0)
    }
}

impl<I: IpExt, D: IpDeviceId, S: SocketMapStateSpec + DatagramPortAlloc> DatagramSockets<I, D, S>
where
    Bound<S>: Tagged<AddrVec<IpPortSpec<I, D>>>,
{
    /// Helper function to allocate a local port.
    ///
    /// Attempts to allocate a new unused local port with the given flow identifier
    /// `id`.
    pub(crate) fn try_alloc_local_port<R: RngCore>(
        &mut self,
        rng: &mut R,
        id: ProtocolFlowId<I::Addr>,
    ) -> Option<NonZeroU16>
    where
        Bound<S>: Tagged<AddrVec<IpPortSpec<I, D>>>,
    {
        let Self { bound, unbound: _, lazy_port_alloc } = self;
        // Lazily init port_alloc if it hasn't been inited yet.
        let port_alloc = lazy_port_alloc.get_or_insert_with(|| PortAlloc::new(rng));
        port_alloc.try_alloc(&id, bound).and_then(NonZeroU16::new)
    }
}

#[derive(Debug, Derivative)]
#[derivative(Default(bound = ""))]
pub(crate) struct MulticastMemberships<A: Eq + Hash, D>(HashSet<(MulticastAddr<A>, D)>);

pub(crate) enum MulticastMembershipChange {
    Join,
    Leave,
}

impl<A: Eq + Hash, D: Eq + Hash> MulticastMemberships<A, D> {
    pub(crate) fn apply_membership_change(
        &mut self,
        address: MulticastAddr<A>,
        device: D,
        want_membership: bool,
    ) -> Option<MulticastMembershipChange> {
        let Self(map) = self;
        if want_membership {
            map.insert((address, device)).then(|| MulticastMembershipChange::Join)
        } else {
            map.remove(&(address, device)).then(|| MulticastMembershipChange::Leave)
        }
    }
}

impl<A: Eq + Hash, D: Eq + Hash> IntoIterator for MulticastMemberships<A, D> {
    type Item = (MulticastAddr<A>, D);
    type IntoIter = <HashSet<(MulticastAddr<A>, D)> as IntoIterator>::IntoIter;

    fn into_iter(self) -> Self::IntoIter {
        let Self(memberships) = self;
        memberships.into_iter()
    }
}

impl<A: IpAddress, D: IpDeviceId> ConnAddr<A, D, NonZeroU16, NonZeroU16> {
    fn from_protocol_flow_and_local_port(id: &ProtocolFlowId<A>, local_port: NonZeroU16) -> Self {
        Self {
            ip: ConnIpAddr {
                local: (*id.local_addr(), local_port),
                remote: (*id.remote_addr(), id.remote_port()),
            },
            device: None,
        }
    }
}
