// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Shared code for implementing datagram sockets.

use alloc::collections::HashSet;
use core::{hash::Hash, num::NonZeroU16};

use rand::RngCore;

use derivative::Derivative;
use net_types::{
    ip::{Ip, IpAddress},
    MulticastAddr, SpecifiedAddr,
};

use crate::{
    algorithm::{PortAlloc, PortAllocImpl, ProtocolFlowId},
    data_structures::{id_map::IdMap, socketmap::Tagged},
    ip::{IpDeviceId, IpExt},
    socket::{
        address::{ConnAddr, ConnIpAddr, IpPortSpec},
        AddrVec, Bound, BoundSocketMap, ListenerAddr, SocketMapAddrSpec, SocketMapAddrStateSpec,
        SocketMapConflictPolicy, SocketMapStateSpec, SocketTypeState as _, SocketTypeStateMut as _,
    },
};

/// Datagram socket storage.
#[derive(Derivative)]
#[derivative(Default(bound = ""))]
pub(crate) struct DatagramSockets<A: SocketMapAddrSpec, S: DatagramSocketSpec>
where
    Bound<S>: Tagged<AddrVec<A>>,
    BoundSocketMap<A, S>: PortAllocImpl,
{
    pub(crate) bound: BoundSocketMap<A, S>,
    pub(crate) unbound: IdMap<UnboundSocketState<A::IpAddr, A::DeviceId, S::UnboundSharingState>>,
    /// lazy_port_alloc is lazy-initialized when it's used.
    pub(crate) lazy_port_alloc: Option<PortAlloc<BoundSocketMap<A, S>>>,
}

#[derive(Debug, Derivative)]
#[derivative(Default(bound = "S: Default"))]
pub(crate) struct UnboundSocketState<A: IpAddress, D, S> {
    pub(crate) device: Option<D>,
    pub(crate) sharing: S,
    pub(crate) multicast_memberships: MulticastMemberships<A, D>,
}

#[derive(Debug)]
pub(crate) struct ListenerState<A: Eq + Hash, D: Hash + Eq> {
    pub(crate) multicast_memberships: MulticastMemberships<A, D>,
}

#[derive(Debug)]
pub(crate) struct ConnState<A: Eq + Hash, D: Eq + Hash, S> {
    pub(crate) socket: S,
    pub(crate) multicast_memberships: MulticastMemberships<A, D>,
}

impl<I: IpExt, D: IpDeviceId, S: DatagramSocketSpec> DatagramSockets<IpPortSpec<I, D>, S>
where
    Bound<S>: Tagged<AddrVec<IpPortSpec<I, D>>>,
    BoundSocketMap<IpPortSpec<I, D>, S>: PortAllocImpl,
{
    /// Helper function to allocate a local port.
    ///
    /// Attempts to allocate a new unused local port with the given flow identifier
    /// `id`.
    pub(crate) fn try_alloc_local_port<R: RngCore>(
        &mut self,
        rng: &mut R,
        id: <BoundSocketMap<IpPortSpec<I, D>, S> as PortAllocImpl>::Id,
    ) -> Option<NonZeroU16> {
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
    pub(crate) fn from_protocol_flow_and_local_port(
        id: &ProtocolFlowId<A>,
        local_port: NonZeroU16,
    ) -> Self {
        Self {
            ip: ConnIpAddr {
                local: (*id.local_addr(), local_port),
                remote: (*id.remote_addr(), id.remote_port()),
            },
            device: None,
        }
    }
}

fn leave_all_joined_groups<
    A: SocketMapAddrSpec,
    S: DatagramSocketSpec,
    C: DatagramStateNonSyncContext<<A::IpAddr as IpAddress>::Version>,
    SC: DatagramStateContext<A, C, S>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    memberships: MulticastMemberships<A::IpAddr, A::DeviceId>,
) {
    for (addr, device) in memberships {
        sync_ctx.leave_multicast_group(ctx, device, addr)
    }
}

pub(crate) trait DatagramStateContext<A: SocketMapAddrSpec, C, S> {
    /// Requests that the specified device join the given multicast group.
    ///
    /// If this method is called multiple times with the same device and
    /// address, the device will remain joined to the multicast group until
    /// [`UdpContext::leave_multicast_group`] has been called the same number of times.
    fn join_multicast_group(
        &mut self,
        ctx: &mut C,
        device: A::DeviceId,
        addr: MulticastAddr<A::IpAddr>,
    );

    /// Requests that the specified device leave the given multicast group.
    ///
    /// Each call to this method must correspond to an earlier call to
    /// [`UdpContext::join_multicast_group`]. The device remains a member of the
    /// multicast group so long as some call to `join_multicast_group` has been
    /// made without a corresponding call to `leave_multicast_group`.
    fn leave_multicast_group(
        &mut self,
        ctx: &mut C,
        device: A::DeviceId,
        addr: MulticastAddr<A::IpAddr>,
    );

    /// Gets the ID for the device that has the given address assigned.
    ///
    /// Returns `None` if no such device exists.
    fn get_device_with_assigned_addr(&self, addr: SpecifiedAddr<A::IpAddr>) -> Option<A::DeviceId>;

    /// Calls the function with an immutable reference to the datagram sockets.
    fn with_sockets<O, F: FnOnce(&DatagramSockets<A, S>) -> O>(&self, cb: F) -> O;

    /// Calls the function with a mutable reference to the datagram sockets.
    fn with_sockets_mut<O, F: FnOnce(&mut DatagramSockets<A, S>) -> O>(&mut self, cb: F) -> O;
}

pub(crate) trait DatagramStateNonSyncContext<I: Ip> {}

pub(crate) trait DatagramSocketSpec: SocketMapStateSpec {
    type UnboundId: Clone + From<usize> + Into<usize>;
    type UnboundSharingState: Default;
}

pub(crate) fn create_unbound<
    A: SocketMapAddrSpec,
    S: DatagramSocketSpec,
    C,
    SC: DatagramStateContext<A, C, S>,
>(
    sync_ctx: &mut SC,
) -> S::UnboundId
where
    Bound<S>: Tagged<AddrVec<A>>,
    BoundSocketMap<A, S>: PortAllocImpl,
{
    sync_ctx.with_sockets_mut(|DatagramSockets { unbound, bound: _, lazy_port_alloc: _ }| {
        unbound.push(UnboundSocketState::default()).into()
    })
}

pub(crate) fn remove_unbound<
    A: SocketMapAddrSpec,
    S: DatagramSocketSpec,
    C: DatagramStateNonSyncContext<<A::IpAddr as IpAddress>::Version>,
    SC: DatagramStateContext<A, C, S>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    id: S::UnboundId,
) where
    Bound<S>: Tagged<AddrVec<A>>,
    BoundSocketMap<A, S>: PortAllocImpl,
{
    let UnboundSocketState { device: _, sharing: _, multicast_memberships } = sync_ctx
        .with_sockets_mut(|state| {
            let DatagramSockets { unbound, bound: _, lazy_port_alloc: _ } = state;
            unbound.remove(id.into()).expect("invalid UDP unbound ID")
        });

    leave_all_joined_groups(sync_ctx, ctx, multicast_memberships);
}

pub(crate) fn remove_listener<
    A: SocketMapAddrSpec,
    C: DatagramStateNonSyncContext<<A::IpAddr as IpAddress>::Version>,
    SC: DatagramStateContext<A, C, S>,
    S: DatagramSocketSpec<ListenerState = ListenerState<A::IpAddr, A::DeviceId>>
        + SocketMapConflictPolicy<
            ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
            <S as SocketMapStateSpec>::ListenerSharingState,
            A,
        >,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    id: S::ListenerId,
) -> ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>
where
    Bound<S>: Tagged<AddrVec<A>>,
    BoundSocketMap<A, S>: PortAllocImpl,
    S::ListenerAddrState:
        SocketMapAddrStateSpec<Id = S::ListenerId, SharingState = S::ListenerSharingState>,
{
    let (ListenerState { multicast_memberships }, addr) = sync_ctx.with_sockets_mut(|state| {
        let DatagramSockets { bound, unbound: _, lazy_port_alloc: _ } = state;
        let (state, _, addr): (_, S::ListenerSharingState, _) =
            bound.listeners_mut().remove(&id).expect("Invalid UDP listener ID");
        (state, addr)
    });

    leave_all_joined_groups(sync_ctx, ctx, multicast_memberships);
    addr
}

pub(crate) fn remove_conn<
    A: SocketMapAddrSpec,
    C: DatagramStateNonSyncContext<<A::IpAddr as IpAddress>::Version>,
    SC: DatagramStateContext<A, C, S>,
    CS,
    S: DatagramSocketSpec<ConnState = ConnState<A::IpAddr, A::DeviceId, CS>>
        + SocketMapConflictPolicy<
            ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
            <S as SocketMapStateSpec>::ConnSharingState,
            A,
        >,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    id: S::ConnId,
) -> ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>
where
    Bound<S>: Tagged<AddrVec<A>>,
    BoundSocketMap<A, S>: PortAllocImpl,
    S::ConnAddrState: SocketMapAddrStateSpec<Id = S::ConnId, SharingState = S::ConnSharingState>,
{
    let (ConnState { socket: _, multicast_memberships }, addr) =
        sync_ctx.with_sockets_mut(|state| {
            let DatagramSockets { bound, unbound: _, lazy_port_alloc: _ } = state;
            let (state, _sharing, addr): (_, S::ConnSharingState, _) =
                bound.conns_mut().remove(&id).expect("UDP connection not found");
            (state, addr)
        });

    leave_all_joined_groups(sync_ctx, ctx, multicast_memberships);
    addr
}

/// Error resulting from attempting to change multicast membership settings for
/// a socket.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum SetMulticastMembershipError {
    /// The provided address does not match the provided device.
    AddressNotAvailable,
    /// The provided address does not match any address on the host.
    NoDeviceWithAddress,
    /// No device or address was specified and there is no device with a route
    /// to the multicast address.
    NoDeviceAvailable,
    /// The requested membership change had no effect (tried to leave a group
    /// without joining, or to join a group again).
    NoMembershipChange,
    /// The socket is bound to a device that doesn't match the one specified.
    WrongDevice,
}

fn pick_matching_interface<
    A: SocketMapAddrSpec,
    S,
    C: DatagramStateNonSyncContext<<A::IpAddr as IpAddress>::Version>,
    SC: DatagramStateContext<A, C, S>,
>(
    sync_ctx: &SC,
    device: Option<A::DeviceId>,
    address: Option<SpecifiedAddr<A::IpAddr>>,
) -> Result<Option<A::DeviceId>, SetMulticastMembershipError> {
    match (device, address) {
        (Some(device), None) => Ok(Some(device)),
        (Some(device), Some(addr)) => sync_ctx
            .get_device_with_assigned_addr(addr)
            .and_then(|found_device| (device == found_device).then(|| device))
            .ok_or(SetMulticastMembershipError::AddressNotAvailable)
            .map(Some),
        (None, Some(addr)) => sync_ctx
            .get_device_with_assigned_addr(addr)
            .ok_or(SetMulticastMembershipError::NoDeviceWithAddress)
            .map(Some),
        (None, None) => Ok(None),
    }
}

fn pick_interface_for_addr<
    A: SocketMapAddrSpec,
    S,
    C: DatagramStateNonSyncContext<<A::IpAddr as IpAddress>::Version>,
    SC: DatagramStateContext<A, C, S>,
>(
    _sync_ctx: &SC,
    _addr: MulticastAddr<A::IpAddr>,
) -> Option<A::DeviceId> {
    log_unimplemented!(
        (),
        "https://fxbug.dev/39479: Implement this by passing `_addr` through the routing table."
    );
    None
}

#[derive(Derivative)]
#[derivative(Clone(bound = "S::UnboundId: Clone, S::ListenerId: Clone, S::ConnId: Clone"))]
#[derivative(Copy(bound = "S::UnboundId: Copy, S::ListenerId: Copy, S::ConnId: Copy"))]
pub(crate) enum DatagramSocketId<S: DatagramSocketSpec> {
    Unbound(S::UnboundId),
    Listener(S::ListenerId),
    Connected(S::ConnId),
}

/// Sets the specified socket's membership status for the given group.
///
/// If `id` is unbound, the membership state will take effect when it is bound.
/// An error is returned if the membership change request is invalid (e.g.
/// leaving a group that was not joined, or joining a group multiple times) or
/// if the device to use to join is unspecified or conflicts with the existing
/// socket state.
pub(crate) fn set_multicast_membership<
    A: SocketMapAddrSpec,
    C: DatagramStateNonSyncContext<<A::IpAddr as IpAddress>::Version>,
    SC: DatagramStateContext<A, C, S>,
    CS,
    S: DatagramSocketSpec<
            ListenerState = ListenerState<A::IpAddr, A::DeviceId>,
            ConnState = ConnState<A::IpAddr, A::DeviceId, CS>,
        > + SocketMapConflictPolicy<
            ListenerAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier>,
            <S as SocketMapStateSpec>::ListenerSharingState,
            A,
        > + SocketMapConflictPolicy<
            ConnAddr<A::IpAddr, A::DeviceId, A::LocalIdentifier, A::RemoteIdentifier>,
            <S as SocketMapStateSpec>::ConnSharingState,
            A,
        >,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    id: impl Into<DatagramSocketId<S>>,
    multicast_group: MulticastAddr<A::IpAddr>,
    local_interface_address: Option<SpecifiedAddr<A::IpAddr>>,
    interface_identifier: Option<A::DeviceId>,
    want_membership: bool,
) -> Result<(), SetMulticastMembershipError>
where
    Bound<S>: Tagged<AddrVec<A>>,
    BoundSocketMap<A, S>: PortAllocImpl,
    S::ListenerAddrState:
        SocketMapAddrStateSpec<Id = S::ListenerId, SharingState = S::ListenerSharingState>,
    S::ConnAddrState: SocketMapAddrStateSpec<Id = S::ConnId, SharingState = S::ConnSharingState>,
{
    let id = id.into();
    let interface =
        pick_matching_interface(sync_ctx, interface_identifier, local_interface_address)?;

    let interface = sync_ctx.with_sockets(|state| {
        let DatagramSockets { bound, unbound, lazy_port_alloc: _ } = state;
        let bound_device = match id.clone() {
            DatagramSocketId::Unbound(id) => {
                let UnboundSocketState { device, sharing: _, multicast_memberships: _ } =
                    unbound.get(id.into()).expect("unbound UDP socket not found");
                device
            }
            DatagramSocketId::Listener(id) => {
                let (_, _, ListenerAddr { ip: _, device }): &(
                    ListenerState<_, _>,
                    S::ListenerSharingState,
                    _,
                ) = bound.listeners().get_by_id(&id).expect("Listening socket not found");
                device
            }
            DatagramSocketId::Connected(id) => {
                let (_, _, ConnAddr { ip: _, device }): &(
                    ConnState<_, _, _>,
                    S::ConnSharingState,
                    _,
                ) = bound.conns().get_by_id(&id).expect("Connected socket not found");
                device
            }
        };

        // If the socket is bound to a device, check that against the provided
        // interface ID. If none was provided, use the bound device. If there is
        // none, try to pick a device using the provided address.
        match (bound_device, interface) {
            (Some(bound_device), None) => Ok(*bound_device),
            (None, Some(interface)) => Ok(interface),
            (Some(bound_device), Some(interface)) => (*bound_device == interface)
                .then(|| interface)
                .ok_or(SetMulticastMembershipError::WrongDevice),
            (None, None) => pick_interface_for_addr(sync_ctx, multicast_group)
                .ok_or(SetMulticastMembershipError::NoDeviceAvailable),
        }
    })?;

    // Re-borrow the state mutably here now that we have picked an interface.
    // This can be folded into the above if we can teach our context traits that
    // the UDP state can be borrowed while the interface picking code runs.
    let change = sync_ctx.with_sockets_mut(|state| {
        let DatagramSockets { bound, unbound, lazy_port_alloc: _ } = state;
        let multicast_memberships = match id {
            DatagramSocketId::Unbound(id) => {
                let UnboundSocketState { device: _, sharing: _, multicast_memberships } =
                    unbound.get_mut(id.into()).expect("unbound UDP socket not found");
                multicast_memberships
            }

            DatagramSocketId::Listener(id) => {
                let (ListenerState { multicast_memberships }, _, _): (
                    _,
                    &S::ListenerSharingState,
                    &ListenerAddr<_, _, _>,
                ) = bound.listeners_mut().get_by_id_mut(&id).expect("Listening socket not found");
                multicast_memberships
            }
            DatagramSocketId::Connected(id) => {
                let (ConnState { socket: _, multicast_memberships }, _, _): (
                    _,
                    &S::ConnSharingState,
                    &ConnAddr<_, _, _, _>,
                ) = bound.conns_mut().get_by_id_mut(&id).expect("Connected socket not found");
                multicast_memberships
            }
        };

        multicast_memberships
            .apply_membership_change(multicast_group, interface, want_membership)
            .ok_or(SetMulticastMembershipError::NoMembershipChange)
    })?;

    match change {
        MulticastMembershipChange::Join => {
            sync_ctx.join_multicast_group(ctx, interface, multicast_group)
        }
        MulticastMembershipChange::Leave => {
            sync_ctx.leave_multicast_group(ctx, interface, multicast_group)
        }
    }

    Ok(())
}
