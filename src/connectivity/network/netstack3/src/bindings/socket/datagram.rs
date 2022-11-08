// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Datagram socket bindings.

use std::{
    collections::VecDeque,
    convert::TryInto as _,
    fmt::Debug,
    marker::PhantomData,
    num::{NonZeroU16, NonZeroU64, NonZeroU8, TryFromIntError},
    ops::{ControlFlow, Deref as _, DerefMut as _},
    sync::Arc,
};

use fidl_fuchsia_io as fio;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_posix as fposix;
use fidl_fuchsia_posix_socket as fposix_socket;

use assert_matches::assert_matches;
use async_utils::stream::OneOrMany;
use explicit::ResultExt as _;
use fidl::endpoints::{ControlHandle as _, RequestStream as _, ServerEnd};
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, prelude::HandleBased as _, Peered as _};
use futures::{FutureExt as _, StreamExt as _, TryFutureExt as _};
use log::{error, trace, warn};
use net_types::{
    ip::{Ip, IpVersion, Ipv4, Ipv6},
    MulticastAddr, SpecifiedAddr, ZonedAddr,
};
use netstack3_core::{
    data_structures::id_map_collection::{IdMapCollection, IdMapCollectionKey},
    device::DeviceId,
    error::{LocalAddressError, SocketError},
    ip::{icmp, socket::IpSockSendError, IpExt},
    socket::datagram::{
        ConnectListenerError, MulticastInterfaceSelector, MulticastMembershipInterfaceSelector,
        SetMulticastMembershipError, SockCreationError,
    },
    sync::Mutex,
    transport::udp::{
        self as core_udp, BufferUdpContext, UdpBoundId, UdpConnId, UdpConnInfo, UdpContext,
        UdpListenerId, UdpListenerInfo, UdpSendError, UdpSendListenerError, UdpSocketId,
        UdpUnboundId,
    },
    BufferNonSyncContext, Ctx, NonSyncContext, SyncCtx,
};
use packet::{Buf, BufferMut, SerializeError};
use packet_formats::{
    error::ParseError,
    icmp::{
        IcmpEchoReply, IcmpEchoRequest, IcmpIpExt, IcmpMessage, IcmpPacket, IcmpPacketBuilder,
        IcmpParseArgs, IcmpUnusedCode,
    },
};
use thiserror::Error;

use crate::bindings::{
    devices::Devices,
    util::{
        self, DeviceNotFoundError, IntoCore as _, TryFromFidlWithContext, TryIntoCore,
        TryIntoCoreWithContext, TryIntoFidlWithContext,
    },
    CommonInfo, LockableContext, StackTime,
};

use super::{
    IntoErrno, IpSockAddrExt, SockAddr, SocketWorkerProperties, ZXSIO_SIGNAL_INCOMING,
    ZXSIO_SIGNAL_OUTGOING,
};

// These values were picked to match Linux behavior.

/// Limits the total size of messages that can be queued for an application
/// socket to be read before we start dropping packets.
const MAX_OUTSTANDING_APPLICATION_MESSAGES_SIZE: usize = 4 * 1024 * 1024;
/// The default value for the amount of data that can be queued for an
/// application socket to be read before packets are dropped.
const DEFAULT_OUTSTANDING_APPLICATION_MESSAGES_SIZE: usize = 208 * 1024;
/// The minimum value for the amount of data that can be queued for an
/// application socket to be read before packets are dropped.
const MIN_OUTSTANDING_APPLICATION_MESSAGES_SIZE: usize = 256;

/// The types of supported datagram protocols.
#[derive(Debug)]
pub(crate) enum DatagramProtocol {
    Udp,
    IcmpEcho,
}

/// A minimal abstraction over transport protocols that allows bindings-side state to be stored.
pub(crate) trait Transport<I>: Debug + Sized {
    const PROTOCOL: DatagramProtocol;
    type UnboundId: Debug + Copy + IdMapCollectionKey + Send + Sync;
    type ConnId: Debug + Copy + IdMapCollectionKey + Send + Sync;
    type ListenerId: Debug + Copy + IdMapCollectionKey + Send + Sync;
}

#[derive(Debug)]
pub(crate) enum BoundSocketId<I, T: Transport<I>> {
    Connected(T::ConnId),
    Listener(T::ListenerId),
}

#[derive(Debug)]
pub(crate) enum SocketId<I, T: Transport<I>> {
    Unbound(T::UnboundId),
    Bound(BoundSocketId<I, T>),
}

impl<I, T: Transport<I>> From<BoundSocketId<I, T>> for SocketId<I, T> {
    fn from(id: BoundSocketId<I, T>) -> Self {
        SocketId::Bound(id)
    }
}

/// Mapping from socket IDs to their receive queues.
///
/// Receive queues are shared between the collections here and the tasks
/// handling socket requests. Since `SocketCollection` implements [`UdpContext`]
/// and [`BufferUdpContext`], whose trait methods may be called from
/// within Core in a locked context, once one of the [`MessageQueue`]s is
/// locked, no calls may be made into [`netstack3_core`]. This prevents
/// a potential deadlock where Core is waiting for a `MessageQueue` to be
/// available and some bindings code here holds the `MessageQueue` and
/// attempts to lock Core state via a `netstack3_core` call.
pub(crate) struct SocketCollection<I: Ip, T: Transport<I>> {
    conns: IdMapCollection<T::ConnId, Arc<Mutex<MessageQueue<I, T>>>>,
    listeners: IdMapCollection<T::ListenerId, Arc<Mutex<MessageQueue<I, T>>>>,
}

impl<I: Ip, T: Transport<I>> Default for SocketCollection<I, T> {
    fn default() -> Self {
        Self { conns: Default::default(), listeners: Default::default() }
    }
}

pub(crate) struct SocketCollectionPair<T>
where
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
{
    v4: SocketCollection<Ipv4, T>,
    v6: SocketCollection<Ipv6, T>,
}

impl<T> Default for SocketCollectionPair<T>
where
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
{
    fn default() -> Self {
        Self { v4: Default::default(), v6: Default::default() }
    }
}

/// An extension trait that allows generic access to IP-specific state.
pub(crate) trait SocketCollectionIpExt<T>: Ip
where
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
    T: Transport<Self>,
{
    fn get_collection<D: AsRef<SocketCollectionPair<T>>>(
        dispatcher: &D,
    ) -> &SocketCollection<Self, T>;

    fn get_collection_mut<D: AsMut<SocketCollectionPair<T>>>(
        dispatcher: &mut D,
    ) -> &mut SocketCollection<Self, T>;
}

impl<T> SocketCollectionIpExt<T> for Ipv4
where
    T: 'static,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
{
    fn get_collection<D: AsRef<SocketCollectionPair<T>>>(
        dispatcher: &D,
    ) -> &SocketCollection<Ipv4, T> {
        &dispatcher.as_ref().v4
    }

    fn get_collection_mut<D: AsMut<SocketCollectionPair<T>>>(
        dispatcher: &mut D,
    ) -> &mut SocketCollection<Ipv4, T> {
        &mut dispatcher.as_mut().v4
    }
}

impl<T> SocketCollectionIpExt<T> for Ipv6
where
    T: 'static,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
{
    fn get_collection<D: AsRef<SocketCollectionPair<T>>>(
        dispatcher: &D,
    ) -> &SocketCollection<Ipv6, T> {
        &dispatcher.as_ref().v6
    }

    fn get_collection_mut<D: AsMut<SocketCollectionPair<T>>>(
        dispatcher: &mut D,
    ) -> &mut SocketCollection<Ipv6, T> {
        &mut dispatcher.as_mut().v6
    }
}

/// A special case of TryFrom that avoids the associated error type in generic contexts.
pub(crate) trait OptionFromU16: Sized {
    fn from_u16(_: u16) -> Option<Self>;
}

/// An abstraction over transport protocols that allows generic manipulation of Core state.
pub(crate) trait TransportState<I: Ip>: Transport<I> {
    type CreateConnError: IntoErrno;
    type CreateListenerError: IntoErrno;
    type ConnectListenerError: IntoErrno;
    type ReconnectConnError: IntoErrno;
    type SetSocketDeviceError: IntoErrno;
    type SetMulticastMembershipError: IntoErrno;
    type LocalIdentifier: OptionFromU16 + Into<u16> + Send;
    type RemoteIdentifier: OptionFromU16 + Into<u16> + Send;

    fn create_unbound<C: NonSyncContext>(ctx: &SyncCtx<C>) -> Self::UnboundId;

    fn connect_unbound<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::UnboundId,
        remote_ip: ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        remote_id: Self::RemoteIdentifier,
    ) -> Result<Self::ConnId, Self::CreateConnError>;

    fn listen_on_unbound<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::UnboundId,
        addr: Option<ZonedAddr<I::Addr, DeviceId<C::Instant>>>,
        port: Option<Self::LocalIdentifier>,
    ) -> Result<Self::ListenerId, Self::CreateListenerError>;

    fn connect_listener<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ListenerId,
        remote_ip: ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        remote_id: Self::RemoteIdentifier,
    ) -> Result<Self::ConnId, (Self::ConnectListenerError, Self::ListenerId)>;

    fn disconnect_connected<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ConnId,
    ) -> Self::ListenerId;

    fn reconnect_conn<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ConnId,
        remote_ip: ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        remote_id: Self::RemoteIdentifier,
    ) -> Result<Self::ConnId, (Self::ReconnectConnError, Self::ConnId)>;

    fn get_conn_info<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ConnId,
    ) -> (
        ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        Self::LocalIdentifier,
        ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        Self::RemoteIdentifier,
    );

    fn get_listener_info<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ListenerId,
    ) -> (Option<ZonedAddr<I::Addr, DeviceId<C::Instant>>>, Self::LocalIdentifier);

    fn remove_conn<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ConnId,
    ) -> (
        ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        Self::LocalIdentifier,
        ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        Self::RemoteIdentifier,
    );

    fn remove_listener<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ListenerId,
    ) -> (Option<ZonedAddr<I::Addr, DeviceId<C::Instant>>>, Self::LocalIdentifier);

    fn remove_unbound<C: NonSyncContext>(sync_ctx: &SyncCtx<C>, ctx: &mut C, id: Self::UnboundId);

    fn set_socket_device<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: SocketId<I, Self>,
        device: Option<&DeviceId<C::Instant>>,
    ) -> Result<(), Self::SetSocketDeviceError>;

    fn get_bound_device<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &C,
        id: SocketId<I, Self>,
    ) -> Option<DeviceId<C::Instant>>;

    fn set_reuse_port<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::UnboundId,
        reuse_port: bool,
    );

    fn get_reuse_port<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &C,
        id: SocketId<I, Self>,
    ) -> bool;

    fn set_multicast_membership<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: SocketId<I, Self>,
        multicast_group: MulticastAddr<I::Addr>,
        interface: MulticastMembershipInterfaceSelector<I::Addr, DeviceId<C::Instant>>,
        want_membership: bool,
    ) -> Result<(), Self::SetMulticastMembershipError>;

    fn set_unicast_hop_limit<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: SocketId<I, Self>,
        hop_limit: Option<NonZeroU8>,
    );

    fn set_multicast_hop_limit<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: SocketId<I, Self>,
        hop_limit: Option<NonZeroU8>,
    );

    fn get_unicast_hop_limit<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &C,
        id: SocketId<I, Self>,
    ) -> NonZeroU8;

    fn get_multicast_hop_limit<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &C,
        id: SocketId<I, Self>,
    ) -> NonZeroU8;
}

/// An abstraction over transport protocols that allows data to be sent via the Core.
pub(crate) trait BufferTransportState<I: Ip, B: BufferMut>: TransportState<I> {
    type SendError: IntoErrno;
    type SendConnError: IntoErrno;
    type SendListenerError: IntoErrno;

    fn send_conn<C: BufferNonSyncContext<B>>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        conn: Self::ConnId,
        body: B,
        remote: Option<(SpecifiedAddr<I::Addr>, Self::RemoteIdentifier)>,
    ) -> Result<(), (B, Self::SendConnError)>;

    fn send_listener<C: BufferNonSyncContext<B>>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        listener: Self::ListenerId,
        local_ip: Option<SpecifiedAddr<I::Addr>>,
        remote_ip: SpecifiedAddr<I::Addr>,
        remote_id: Self::RemoteIdentifier,
        body: B,
    ) -> Result<(), (B, Self::SendListenerError)>;
}

#[derive(Debug)]
pub(crate) enum Udp {}

impl<I: Ip> Transport<I> for Udp {
    const PROTOCOL: DatagramProtocol = DatagramProtocol::Udp;
    type UnboundId = UdpUnboundId<I>;
    type ConnId = UdpConnId<I>;
    type ListenerId = UdpListenerId<I>;
}

impl<I: Ip> From<BoundSocketId<I, Udp>> for UdpBoundId<I> {
    fn from(id: BoundSocketId<I, Udp>) -> Self {
        match id {
            BoundSocketId::Connected(c) => UdpBoundId::Connected(c),
            BoundSocketId::Listener(c) => UdpBoundId::Listening(c),
        }
    }
}

impl<I: Ip> From<SocketId<I, Udp>> for UdpSocketId<I> {
    fn from(id: SocketId<I, Udp>) -> Self {
        match id {
            SocketId::Unbound(id) => Self::Unbound(id),
            SocketId::Bound(id) => Self::Bound(id.into()),
        }
    }
}

impl OptionFromU16 for NonZeroU16 {
    fn from_u16(t: u16) -> Option<Self> {
        Self::new(t)
    }
}

impl<I: IpExt> TransportState<I> for Udp {
    type CreateConnError = SockCreationError;
    type CreateListenerError = LocalAddressError;
    type ConnectListenerError = ConnectListenerError;
    type ReconnectConnError = ConnectListenerError;
    type SetSocketDeviceError = SocketError;
    type SetMulticastMembershipError = SetMulticastMembershipError;
    type LocalIdentifier = NonZeroU16;
    type RemoteIdentifier = NonZeroU16;

    fn create_unbound<C: NonSyncContext>(ctx: &SyncCtx<C>) -> Self::UnboundId {
        core_udp::create_udp_unbound(ctx)
    }

    fn connect_unbound<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::UnboundId,
        remote_ip: ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        remote_id: Self::RemoteIdentifier,
    ) -> Result<Self::ConnId, Self::CreateConnError> {
        core_udp::connect_udp(sync_ctx, ctx, id, remote_ip, remote_id)
    }

    fn listen_on_unbound<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::UnboundId,
        addr: Option<ZonedAddr<I::Addr, DeviceId<C::Instant>>>,
        port: Option<Self::LocalIdentifier>,
    ) -> Result<Self::ListenerId, Self::CreateListenerError> {
        core_udp::listen_udp(sync_ctx, ctx, id, addr, port)
    }

    fn connect_listener<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ListenerId,
        remote_ip: ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        remote_id: Self::RemoteIdentifier,
    ) -> Result<Self::ConnId, (Self::ConnectListenerError, Self::ListenerId)> {
        core_udp::connect_udp_listener(sync_ctx, ctx, id, remote_ip, remote_id)
    }

    fn disconnect_connected<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ConnId,
    ) -> Self::ListenerId {
        core_udp::disconnect_udp_connected(sync_ctx, ctx, id)
    }

    fn reconnect_conn<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ConnId,
        remote_ip: ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        remote_id: Self::RemoteIdentifier,
    ) -> Result<Self::ConnId, (Self::ReconnectConnError, Self::ConnId)> {
        core_udp::reconnect_udp(sync_ctx, ctx, id, remote_ip, remote_id)
    }

    fn get_conn_info<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ConnId,
    ) -> (
        ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        Self::LocalIdentifier,
        ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        Self::RemoteIdentifier,
    ) {
        let UdpConnInfo { local_ip, local_port, remote_ip, remote_port } =
            core_udp::get_udp_conn_info(sync_ctx, ctx, id);
        (local_ip, local_port, remote_ip, remote_port)
    }

    fn get_listener_info<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ListenerId,
    ) -> (Option<ZonedAddr<I::Addr, DeviceId<C::Instant>>>, Self::LocalIdentifier) {
        let UdpListenerInfo { local_ip, local_port } =
            core_udp::get_udp_listener_info(sync_ctx, ctx, id);
        (local_ip, local_port)
    }

    fn remove_conn<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ConnId,
    ) -> (
        ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        Self::LocalIdentifier,
        ZonedAddr<I::Addr, DeviceId<C::Instant>>,
        Self::RemoteIdentifier,
    ) {
        let UdpConnInfo { local_ip, local_port, remote_ip, remote_port } =
            core_udp::remove_udp_conn(sync_ctx, ctx, id);
        (local_ip, local_port, remote_ip, remote_port)
    }

    fn remove_listener<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::ListenerId,
    ) -> (Option<ZonedAddr<I::Addr, DeviceId<C::Instant>>>, Self::LocalIdentifier) {
        let UdpListenerInfo { local_ip, local_port } =
            core_udp::remove_udp_listener(sync_ctx, ctx, id);
        (local_ip, local_port)
    }

    fn remove_unbound<C: NonSyncContext>(sync_ctx: &SyncCtx<C>, ctx: &mut C, id: Self::UnboundId) {
        core_udp::remove_udp_unbound(sync_ctx, ctx, id)
    }

    fn set_socket_device<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: SocketId<I, Self>,
        device: Option<&DeviceId<C::Instant>>,
    ) -> Result<(), Self::SetSocketDeviceError> {
        match id {
            SocketId::Unbound(id) => {
                core_udp::set_unbound_udp_device(sync_ctx, ctx, id, device);
                Ok(())
            }
            SocketId::Bound(id) => match id {
                BoundSocketId::Listener(id) => {
                    core_udp::set_listener_udp_device(sync_ctx, ctx, id, device)
                        .map_err(SocketError::Local)
                }
                BoundSocketId::Connected(id) => {
                    core_udp::set_conn_udp_device(sync_ctx, ctx, id, device)
                }
            },
        }
    }

    fn get_bound_device<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &C,
        id: SocketId<I, Self>,
    ) -> Option<DeviceId<C::Instant>> {
        core_udp::get_udp_bound_device(sync_ctx, ctx, id.into())
    }

    fn set_reuse_port<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: Self::UnboundId,
        reuse_port: bool,
    ) {
        core_udp::set_udp_posix_reuse_port(sync_ctx, ctx, id, reuse_port)
    }

    fn get_reuse_port<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &C,
        id: SocketId<I, Self>,
    ) -> bool {
        core_udp::get_udp_posix_reuse_port(sync_ctx, ctx, id.into())
    }

    fn set_multicast_membership<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: SocketId<I, Self>,
        multicast_group: MulticastAddr<I::Addr>,
        interface: MulticastMembershipInterfaceSelector<I::Addr, DeviceId<C::Instant>>,
        want_membership: bool,
    ) -> Result<(), Self::SetMulticastMembershipError> {
        core_udp::set_udp_multicast_membership(
            sync_ctx,
            ctx,
            id.into(),
            multicast_group,
            interface,
            want_membership,
        )
    }

    fn set_unicast_hop_limit<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: SocketId<I, Self>,
        hop_limit: Option<NonZeroU8>,
    ) {
        core_udp::set_udp_unicast_hop_limit(sync_ctx, ctx, id.into(), hop_limit)
    }

    fn set_multicast_hop_limit<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        id: SocketId<I, Self>,
        hop_limit: Option<NonZeroU8>,
    ) {
        core_udp::set_udp_multicast_hop_limit(sync_ctx, ctx, id.into(), hop_limit)
    }

    fn get_unicast_hop_limit<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &C,
        id: SocketId<I, Self>,
    ) -> NonZeroU8 {
        core_udp::get_udp_unicast_hop_limit(sync_ctx, ctx, id.into())
    }

    fn get_multicast_hop_limit<C: NonSyncContext>(
        sync_ctx: &SyncCtx<C>,
        ctx: &C,
        id: SocketId<I, Self>,
    ) -> NonZeroU8 {
        core_udp::get_udp_multicast_hop_limit(sync_ctx, ctx, id.into())
    }
}

impl<I: IpExt, B: BufferMut> BufferTransportState<I, B> for Udp {
    type SendError = UdpSendError;
    type SendConnError = UdpSendError;
    type SendListenerError = UdpSendListenerError;

    fn send_conn<C: BufferNonSyncContext<B>>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        conn: Self::ConnId,
        body: B,
        remote: Option<(SpecifiedAddr<I::Addr>, Self::RemoteIdentifier)>,
    ) -> Result<(), (B, Self::SendConnError)> {
        match remote {
            None => core_udp::send_udp_conn(sync_ctx, ctx, conn, body)
                .map_err(|(b, e)| (b, UdpSendError::Send(e))),
            Some((remote_ip, remote_port)) => core_udp::send_udp_conn_to(
                sync_ctx,
                ctx,
                conn,
                ZonedAddr::Unzoned(remote_ip),
                remote_port,
                body,
            ),
        }
    }

    fn send_listener<C: BufferNonSyncContext<B>>(
        sync_ctx: &SyncCtx<C>,
        ctx: &mut C,
        listener: Self::ListenerId,
        _local_ip: Option<SpecifiedAddr<I::Addr>>,
        remote_ip: SpecifiedAddr<I::Addr>,
        remote_id: Self::RemoteIdentifier,
        body: B,
    ) -> Result<(), (B, Self::SendListenerError)> {
        core_udp::send_udp_listener(sync_ctx, ctx, listener, remote_ip, remote_id, body)
    }
}

impl<I: icmp::IcmpIpExt> UdpContext<I> for SocketCollection<I, Udp> {
    fn receive_icmp_error(&mut self, id: UdpBoundId<I>, err: I::ErrorCode) {
        let Self { conns, listeners } = self;
        let id = match &id {
            UdpBoundId::Connected(conn) => conns.get(conn),
            UdpBoundId::Listening(listener) => listeners.get(listener),
        };
        // NB: Logging at error as a means of failing tests that provoke this condition.
        error!("unimplemented receive_icmp_error {:?} on {:?}", err, id)
    }
}

impl<I: IpExt, B: BufferMut> BufferUdpContext<I, B> for SocketCollection<I, Udp> {
    fn receive_udp_from_conn(
        &mut self,
        conn: UdpConnId<I>,
        src_ip: I::Addr,
        src_port: NonZeroU16,
        body: &B,
    ) {
        let Self { conns, listeners: _ } = self;
        let conn = conns.get(&conn).unwrap();
        conn.lock().receive(src_ip, src_port.get(), body.as_ref())
    }

    fn receive_udp_from_listen(
        &mut self,
        listener: UdpListenerId<I>,
        src_ip: I::Addr,
        _dst_ip: I::Addr,
        src_port: Option<NonZeroU16>,
        body: &B,
    ) {
        let Self { conns: _, listeners } = self;
        let listeners = listeners.get(&listener).unwrap();
        listeners.lock().receive(src_ip, src_port.map_or(0, NonZeroU16::get), body.as_ref())
    }
}

// NB: the POSIX API for ICMP sockets operates on ICMP packets in both directions. In other words,
// the calling process is expected to send complete ICMP packets and will likewise receive complete
// ICMP packets on reads - header and all. Note that outbound ICMP packets are parsed and validated
// before being sent on the wire.
#[derive(Debug)]
pub enum IcmpEcho {}

// TODO(https://fxbug.dev/47321): this uninhabited type is a stand-in; the real type needs to be
// defined in the Core.
#[derive(Clone, Copy, Debug)]
pub(crate) enum IcmpListenerId {}

impl IdMapCollectionKey for IcmpListenerId {
    const VARIANT_COUNT: usize = 0;

    fn get_variant(&self) -> usize {
        match *self {}
    }

    fn get_id(&self) -> usize {
        match *self {}
    }
}

impl<I: Ip> Transport<I> for IcmpEcho {
    const PROTOCOL: DatagramProtocol = DatagramProtocol::IcmpEcho;
    type UnboundId = icmp::IcmpUnboundId<I>;
    type ConnId = icmp::IcmpConnId<I>;
    type ListenerId = IcmpListenerId;
}

pub(crate) struct IcmpRemoteIdentifier;

impl Into<u16> for IcmpRemoteIdentifier {
    fn into(self) -> u16 {
        // TODO(https://fxbug.dev/47321): unclear that this is the right thing to do. This is only
        // used in the implementation of getpeername, we should test to see what this does on
        // Linux.
        0
    }
}

impl OptionFromU16 for IcmpRemoteIdentifier {
    fn from_u16(_: u16) -> Option<Self> {
        // TODO(https://fxbug.dev/47321): unclear that this is the right thing to do. This is only
        // used in the implementation of connect, we should test to see what this does on Linux. We
        // may need to store the value so that we can spit it back out in getpeername.
        Some(Self)
    }
}

#[derive(Error, Debug)]
pub(crate) enum IcmpSendError {
    #[error(transparent)]
    IpSock(#[from] IpSockSendError),
    #[error(transparent)]
    ParseError(#[from] ParseError),
}

impl IntoErrno for IcmpSendError {
    fn into_errno(self) -> fposix::Errno {
        match self {
            Self::IpSock(e) => e.into_errno(),
            Self::ParseError(e) => match e {
                ParseError::NotSupported
                | ParseError::NotExpected
                | ParseError::Checksum
                | ParseError::Format => fposix::Errno::Einval,
            },
        }
    }
}

/// An extension trait that allows generic access to IP-specific ICMP functionality in the Core.
pub(crate) trait IcmpEchoIpExt: IcmpIpExt {
    fn new_icmp_unbound<NonSyncCtx: NonSyncContext>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
    ) -> icmp::IcmpUnboundId<Self>;

    fn remove_icmp_unbound<NonSyncCtx: NonSyncContext>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        unbound: icmp::IcmpUnboundId<Self>,
    );

    fn new_icmp_connection<NonSyncCtx: NonSyncContext>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        unbound: icmp::IcmpUnboundId<Self>,
        remote_addr: SpecifiedAddr<Self::Addr>,
    ) -> Result<icmp::IcmpConnId<Self>, icmp::IcmpSockCreationError>;

    fn send_icmp_echo_request<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        conn: icmp::IcmpConnId<Self>,
        seq: u16,
        body: B,
    ) -> Result<(), (B, IcmpSendError)>;

    fn send_conn<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        conn: icmp::IcmpConnId<Self>,
        mut body: B,
    ) -> Result<(), (B, IcmpSendError)>
    where
        IcmpEchoRequest: for<'a> IcmpMessage<Self, &'a [u8]>,
    {
        use net_types::Witness as _;

        let (src_ip, _id, dst_ip, IcmpRemoteIdentifier {}) =
            IcmpEcho::get_conn_info(sync_ctx, ctx, conn);
        let (src_ip, _) = src_ip.into_addr_zone();
        let (dst_ip, _) = dst_ip.into_addr_zone();
        let packet = {
            // This cruft (putting this logic inside a block, assigning to the
            // temporary variable `res` rather than inlining this expression
            // inside of the match argument, and manually dropping `res`) is
            // required because, without it, the borrow checker believes that
            // `body` is still borrowed by the `Result` returned from
            // `parse_with` when it is moved in `return Err((body,
            // err.into()))`.
            //
            // Storing first into `res` allows us to explicitly drop it before
            // moving `body`, which satisfies the borrow checker. We do this
            // inside of a block because if we instead did it at the top level
            // of the function, `res` would live until the end of the function,
            // and would conflict with `body` being moved into
            // `send_icmp_echo_request`. This way, `res` only lives until the
            // end of this block.
            let res = body.parse_with::<_, IcmpPacket<Self, _, IcmpEchoRequest>>(
                IcmpParseArgs::new(src_ip.get(), dst_ip.get()),
            );
            match res {
                Ok(packet) => packet,
                Err(err) => {
                    std::mem::drop(res);
                    return Err((body, err.into()));
                }
            }
        };
        let message = packet.message();
        let seq = message.seq();
        // Drop the packet so we can reuse `body`, which now holds the ICMP
        // packet's body. This is fragile; we should perhaps expose a mutable
        // getter instead.
        std::mem::drop(packet);
        Self::send_icmp_echo_request(sync_ctx, ctx, conn, seq, body)
    }
}

impl IcmpEchoIpExt for Ipv4 {
    fn new_icmp_unbound<NonSyncCtx: NonSyncContext>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
    ) -> icmp::IcmpUnboundId<Self> {
        icmp::create_icmpv4_unbound(sync_ctx)
    }

    fn remove_icmp_unbound<NonSyncCtx: NonSyncContext>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        unbound: icmp::IcmpUnboundId<Self>,
    ) {
        icmp::remove_icmpv4_unbound(sync_ctx, unbound)
    }

    fn new_icmp_connection<NonSyncCtx: NonSyncContext>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        unbound: icmp::IcmpUnboundId<Ipv4>,
        remote_addr: SpecifiedAddr<Self::Addr>,
    ) -> Result<icmp::IcmpConnId<Self>, icmp::IcmpSockCreationError> {
        icmp::connect_icmpv4(sync_ctx, ctx, unbound, None, remote_addr, 0)
    }

    fn send_icmp_echo_request<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        conn: icmp::IcmpConnId<Self>,
        seq: u16,
        body: B,
    ) -> Result<(), (B, IcmpSendError)> {
        icmp::send_icmpv4_echo_request(sync_ctx, ctx, conn, seq, body)
            .map_err(|(body, err)| (body, err.into()))
    }
}

impl IcmpEchoIpExt for Ipv6 {
    fn new_icmp_unbound<NonSyncCtx: NonSyncContext>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
    ) -> icmp::IcmpUnboundId<Self> {
        icmp::create_icmpv6_unbound(sync_ctx)
    }

    fn remove_icmp_unbound<NonSyncCtx: NonSyncContext>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        unbound: icmp::IcmpUnboundId<Self>,
    ) {
        icmp::remove_icmpv6_unbound(sync_ctx, unbound)
    }

    fn new_icmp_connection<NonSyncCtx: NonSyncContext>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        unbound: icmp::IcmpUnboundId<Ipv6>,
        remote_addr: SpecifiedAddr<Self::Addr>,
    ) -> Result<icmp::IcmpConnId<Self>, icmp::IcmpSockCreationError> {
        icmp::connect_icmpv6(sync_ctx, ctx, unbound, None, remote_addr, 0)
    }

    fn send_icmp_echo_request<B: BufferMut, NonSyncCtx: BufferNonSyncContext<B>>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        conn: icmp::IcmpConnId<Self>,
        seq: u16,
        body: B,
    ) -> Result<(), (B, IcmpSendError)> {
        icmp::send_icmpv6_echo_request(sync_ctx, ctx, conn, seq, body)
            .map_err(|(body, err)| (body, err.into()))
    }
}

impl OptionFromU16 for u16 {
    fn from_u16(t: u16) -> Option<Self> {
        Some(t)
    }
}

impl<I: IcmpEchoIpExt> TransportState<I> for IcmpEcho {
    type CreateConnError = icmp::IcmpSockCreationError;
    type CreateListenerError = icmp::IcmpSockCreationError;
    type ConnectListenerError = icmp::IcmpSockCreationError;
    type ReconnectConnError = icmp::IcmpSockCreationError;
    type SetSocketDeviceError = LocalAddressError;
    type SetMulticastMembershipError = LocalAddressError;
    type LocalIdentifier = u16;
    type RemoteIdentifier = IcmpRemoteIdentifier;

    fn create_unbound<NonSyncCtx: NonSyncContext>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
    ) -> Self::UnboundId {
        I::new_icmp_unbound(sync_ctx)
    }

    fn connect_unbound<NonSyncCtx: NonSyncContext>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        id: Self::UnboundId,
        remote_addr: ZonedAddr<I::Addr, DeviceId<NonSyncCtx::Instant>>,
        remote_id: Self::RemoteIdentifier,
    ) -> Result<Self::ConnId, Self::CreateConnError> {
        let IcmpRemoteIdentifier {} = remote_id;
        // TODO(https://fxbug.dev/105494): Handle scoped addresses correctly.
        let (remote_ip, _zone): (_, Option<_>) = remote_addr.into_addr_zone();
        I::new_icmp_connection(sync_ctx, ctx, id, remote_ip)
    }

    fn listen_on_unbound<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _id: Self::UnboundId,
        _addr: Option<ZonedAddr<I::Addr, DeviceId<NonSyncCtx::Instant>>>,
        _stream_id: Option<Self::LocalIdentifier>,
    ) -> Result<Self::ListenerId, Self::CreateListenerError> {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn connect_listener<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _id: Self::ListenerId,
        _remote_ip: ZonedAddr<I::Addr, DeviceId<NonSyncCtx::Instant>>,
        _remote_id: Self::RemoteIdentifier,
    ) -> Result<Self::ConnId, (Self::ConnectListenerError, Self::ListenerId)> {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn disconnect_connected<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _id: Self::ConnId,
    ) -> Self::ListenerId {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn reconnect_conn<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _id: Self::ConnId,
        _remote_ip: ZonedAddr<I::Addr, DeviceId<NonSyncCtx::Instant>>,
        _remote_id: Self::RemoteIdentifier,
    ) -> Result<Self::ConnId, (Self::ConnectListenerError, Self::ConnId)> {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn get_conn_info<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _id: Self::ConnId,
    ) -> (
        ZonedAddr<I::Addr, DeviceId<NonSyncCtx::Instant>>,
        Self::LocalIdentifier,
        ZonedAddr<I::Addr, DeviceId<NonSyncCtx::Instant>>,
        Self::RemoteIdentifier,
    ) {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn get_listener_info<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _id: Self::ListenerId,
    ) -> (Option<ZonedAddr<I::Addr, DeviceId<NonSyncCtx::Instant>>>, Self::LocalIdentifier) {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn remove_conn<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _id: Self::ConnId,
    ) -> (
        ZonedAddr<I::Addr, DeviceId<NonSyncCtx::Instant>>,
        Self::LocalIdentifier,
        ZonedAddr<I::Addr, DeviceId<NonSyncCtx::Instant>>,
        Self::RemoteIdentifier,
    ) {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn remove_listener<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _id: Self::ListenerId,
    ) -> (Option<ZonedAddr<I::Addr, DeviceId<NonSyncCtx::Instant>>>, Self::LocalIdentifier) {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn remove_unbound<NonSyncCtx: NonSyncContext>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        id: Self::UnboundId,
    ) {
        I::remove_icmp_unbound(sync_ctx, id)
    }

    fn set_socket_device<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _id: SocketId<I, Self>,
        _device: Option<&DeviceId<NonSyncCtx::Instant>>,
    ) -> Result<(), Self::SetSocketDeviceError> {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn get_bound_device<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &NonSyncCtx,
        _id: SocketId<I, Self>,
    ) -> Option<DeviceId<NonSyncCtx::Instant>> {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn set_reuse_port<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _id: Self::UnboundId,
        _reuse_port: bool,
    ) {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn get_reuse_port<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &NonSyncCtx,
        _id: SocketId<I, Self>,
    ) -> bool {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn set_multicast_membership<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _id: SocketId<I, Self>,
        _multicast_group: MulticastAddr<I::Addr>,
        _interface: MulticastMembershipInterfaceSelector<I::Addr, DeviceId<NonSyncCtx::Instant>>,
        _want_membership: bool,
    ) -> Result<(), Self::SetMulticastMembershipError> {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn set_unicast_hop_limit<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _id: SocketId<I, Self>,
        _hop_limit: Option<NonZeroU8>,
    ) {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn set_multicast_hop_limit<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _id: SocketId<I, Self>,
        _hop_limit: Option<NonZeroU8>,
    ) {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn get_unicast_hop_limit<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &NonSyncCtx,
        _id: SocketId<I, Self>,
    ) -> NonZeroU8 {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn get_multicast_hop_limit<NonSyncCtx: NonSyncContext>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &NonSyncCtx,
        _id: SocketId<I, Self>,
    ) -> NonZeroU8 {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }
}

impl<I: IcmpEchoIpExt, B: BufferMut> BufferTransportState<I, B> for IcmpEcho
where
    IcmpEchoRequest: for<'a> IcmpMessage<I, &'a [u8]>,
{
    type SendError = IcmpSendError;
    type SendConnError = IcmpSendError;
    type SendListenerError = IcmpSendError;

    fn send_conn<NonSyncCtx: BufferNonSyncContext<B>>(
        sync_ctx: &SyncCtx<NonSyncCtx>,
        ctx: &mut NonSyncCtx,
        conn: Self::ConnId,
        body: B,
        remote: Option<(SpecifiedAddr<I::Addr>, Self::RemoteIdentifier)>,
    ) -> Result<(), (B, Self::SendConnError)> {
        match remote {
            None => I::send_conn(sync_ctx, ctx, conn, body),
            Some((_remote_ip, IcmpRemoteIdentifier {})) => {
                todo!("https://fxbug.dev/47321: needs Core implementation")
            }
        }
    }

    fn send_listener<NonSyncCtx: BufferNonSyncContext<B>>(
        _sync_ctx: &SyncCtx<NonSyncCtx>,
        _ctx: &mut NonSyncCtx,
        _listener: Self::ListenerId,
        _local_ip: Option<SpecifiedAddr<I::Addr>>,
        _remote_ip: SpecifiedAddr<I::Addr>,
        _remote_id: Self::RemoteIdentifier,
        _body: B,
    ) -> Result<(), (B, Self::SendListenerError)> {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }
}

impl<I: icmp::IcmpIpExt> icmp::IcmpContext<I> for SocketCollection<I, IcmpEcho> {
    fn receive_icmp_error(&mut self, conn: icmp::IcmpConnId<I>, seq_num: u16, err: I::ErrorCode) {
        let Self { conns, listeners: _ } = self;
        let conn_data = conns.get(&conn).unwrap();
        // NB: Logging at error as a means of failing tests that provoke this condition.
        error!("unimplemented receive_icmp_error {:?} seq={} on {:?}", err, seq_num, conn_data)
    }
}

impl<I: icmp::IcmpIpExt, B: BufferMut> icmp::BufferIcmpContext<I, B>
    for SocketCollection<I, IcmpEcho>
where
    IcmpEchoReply: for<'a> IcmpMessage<I, &'a [u8], Code = IcmpUnusedCode>,
{
    fn receive_icmp_echo_reply(
        &mut self,
        conn: icmp::IcmpConnId<I>,
        src_ip: I::Addr,
        dst_ip: I::Addr,
        id: u16,
        seq_num: u16,
        data: B,
    ) {
        use packet::Serializer as _;

        match data
            .encapsulate(IcmpPacketBuilder::<I, _, _>::new(
                src_ip,
                dst_ip,
                IcmpUnusedCode,
                IcmpEchoReply::new(id, seq_num),
            ))
            .serialize_vec_outer()
        {
            Ok(body) => {
                let Self { conns, listeners: _ } = self;
                let available = conns.get(&conn).unwrap();
                available.lock().receive(src_ip, id, body.as_ref())
            }
            Err((err, serializer)) => {
                let _: packet::serialize::Nested<B, IcmpPacketBuilder<_, _, _>> = serializer;
                match err {
                    SerializeError::Alloc(never) => match never {},
                    SerializeError::Mtu => panic!("MTU constraint exceeded but not provided"),
                }
            }
        }
    }
}

#[derive(Clone, Debug)]
struct AvailableMessage<A> {
    source_addr: A,
    source_port: u16,
    data: Vec<u8>,
}

#[derive(Debug)]
struct AvailableMessageQueue<A> {
    available_messages: VecDeque<AvailableMessage<A>>,
    /// The total size of the contents of `available_messages`.
    available_messages_size: usize,
    /// The maximum allowed value for `available_messages_size`.
    max_available_messages_size: usize,
}

impl<A> AvailableMessageQueue<A> {
    fn new() -> Self {
        Self {
            available_messages: Default::default(),
            available_messages_size: 0,
            max_available_messages_size: DEFAULT_OUTSTANDING_APPLICATION_MESSAGES_SIZE,
        }
    }

    fn push(&mut self, source_addr: A, source_port: u16, body: &[u8]) -> Result<(), NoSpace> {
        let Self { available_messages, available_messages_size, max_available_messages_size } =
            self;

        // Respect the configured limit except if this would be the only message
        // in the buffer. This is compatible with Linux behavior.
        if *available_messages_size + body.len() > *max_available_messages_size
            && !available_messages.is_empty()
        {
            return Err(NoSpace);
        }

        available_messages.push_back(AvailableMessage {
            source_addr,
            source_port,
            data: body.to_owned(),
        });
        *available_messages_size += body.len();
        Ok(())
    }

    fn pop(&mut self) -> Option<AvailableMessage<A>> {
        let Self { available_messages, available_messages_size, max_available_messages_size: _ } =
            self;

        available_messages.pop_front().map(|msg| {
            *available_messages_size -= msg.data.len();
            msg
        })
    }

    fn peek(&self) -> Option<&AvailableMessage<A>> {
        let Self { available_messages, available_messages_size: _, max_available_messages_size: _ } =
            self;
        available_messages.front()
    }

    fn is_empty(&self) -> bool {
        let Self { available_messages, available_messages_size: _, max_available_messages_size: _ } =
            self;
        available_messages.is_empty()
    }
}

#[derive(Copy, Clone, Debug, Error, Eq, PartialEq)]
#[error("application buffers are full")]
struct NoSpace;

#[derive(Debug)]
struct MessageQueue<I: Ip, T> {
    local_event: zx::EventPair,
    queue: AvailableMessageQueue<I::Addr>,
    _marker: PhantomData<T>,
}

impl<I: Ip, T: Transport<I>> MessageQueue<I, T> {
    fn peek(&self) -> Option<&AvailableMessage<I::Addr>> {
        let Self { queue, local_event: _, _marker } = self;
        queue.peek()
    }

    fn pop(&mut self) -> Option<AvailableMessage<I::Addr>> {
        let Self { queue, local_event, _marker } = self;
        let message = queue.pop();
        if queue.is_empty() {
            if let Err(e) = local_event.signal_peer(ZXSIO_SIGNAL_INCOMING, zx::Signals::NONE) {
                error!("socket failed to signal peer: {:?}", e);
            }
        }
        message
    }

    fn receive(&mut self, addr: I::Addr, port: u16, body: &[u8]) {
        let Self { queue, local_event, _marker } = self;
        match queue.push(addr, port, body) {
            Err(NoSpace) => trace!(
                "dropping {:?} packet from {:?}:{:?} because the receive queue is full",
                T::PROTOCOL,
                addr,
                port
            ),
            Ok(()) => local_event
                .signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_INCOMING)
                .unwrap_or_else(|e| error!("receive_udp_from_conn failed: {:?}", e)),
        }
    }
}

#[derive(Debug)]
struct BindingData<I: Ip, T: Transport<I>> {
    peer_event: zx::EventPair,
    info: SocketControlInfo<I, T>,
    /// The queue for messages received on this socket.
    ///
    /// The message queue is held here and also in the [`SocketCollection`]
    /// to which the socket belongs.
    messages: Arc<Mutex<MessageQueue<I, T>>>,
}

impl<I: Ip, T: Transport<I>> BindingData<I, T> {
    /// Creates a new `BindingData` with the provided event pair and
    /// `properties`.
    fn new(
        unbound_id: T::UnboundId,
        local_event: zx::EventPair,
        peer_event: zx::EventPair,
        properties: SocketWorkerProperties,
    ) -> Self {
        Self {
            peer_event,
            info: SocketControlInfo {
                _properties: properties,
                state: SocketState::Unbound { unbound_id },
            },
            messages: Arc::new(Mutex::new(MessageQueue {
                queue: AvailableMessageQueue::new(),
                local_event,
                _marker: PhantomData,
            })),
        }
    }
}

/// Information on socket control plane.
#[derive(Debug)]
pub(crate) struct SocketControlInfo<I: Ip, T: Transport<I>> {
    _properties: SocketWorkerProperties,
    state: SocketState<I, T>,
}

/// Possible states for a datagram socket.
#[derive(Debug)]
enum SocketState<I: Ip, T: Transport<I>> {
    Unbound { unbound_id: T::UnboundId },
    BoundListen { listener_id: T::ListenerId },
    BoundConnect { conn_id: T::ConnId, shutdown_read: bool, shutdown_write: bool },
}

impl<'a, I: Ip, T: Transport<I>> From<&'a SocketState<I, T>> for SocketId<I, T> {
    fn from(id: &'a SocketState<I, T>) -> Self {
        match id {
            SocketState::Unbound { unbound_id } => SocketId::Unbound(*unbound_id),
            SocketState::BoundListen { listener_id } => {
                BoundSocketId::Listener(*listener_id).into()
            }
            SocketState::BoundConnect { conn_id, shutdown_read: _, shutdown_write: _ } => {
                BoundSocketId::Connected(*conn_id).into()
            }
        }
    }
}

pub(crate) trait SocketWorkerDispatcher:
    RequestHandlerDispatcher<Ipv4, Udp>
    + RequestHandlerDispatcher<Ipv6, Udp>
    + RequestHandlerDispatcher<Ipv4, IcmpEcho>
    + RequestHandlerDispatcher<Ipv6, IcmpEcho>
{
}

impl<T> SocketWorkerDispatcher for T
where
    T: RequestHandlerDispatcher<Ipv4, Udp>,
    T: RequestHandlerDispatcher<Ipv6, Udp>,
    T: RequestHandlerDispatcher<Ipv4, IcmpEcho>,
    T: RequestHandlerDispatcher<Ipv6, IcmpEcho>,
{
}

pub(super) fn spawn_worker<C>(
    domain: fposix_socket::Domain,
    proto: fposix_socket::DatagramSocketProtocol,
    ctx: C,
    events: fposix_socket::SynchronousDatagramSocketRequestStream,
    properties: SocketWorkerProperties,
) -> Result<(), fposix::Errno>
where
    C: LockableContext,
    C::NonSyncCtx: SocketWorkerDispatcher + AsRef<Devices<DeviceId<StackTime>>>,
    C: Clone + Send + Sync + 'static,
{
    match (domain, proto) {
        (fposix_socket::Domain::Ipv4, fposix_socket::DatagramSocketProtocol::Udp) => {
            SocketWorker::<Ipv4, Udp, C>::spawn(ctx, properties, events)
        }
        (fposix_socket::Domain::Ipv6, fposix_socket::DatagramSocketProtocol::Udp) => {
            SocketWorker::<Ipv6, Udp, C>::spawn(ctx, properties, events)
        }
        (fposix_socket::Domain::Ipv4, fposix_socket::DatagramSocketProtocol::IcmpEcho) => {
            SocketWorker::<Ipv4, IcmpEcho, C>::spawn(ctx, properties, events)
        }
        (fposix_socket::Domain::Ipv6, fposix_socket::DatagramSocketProtocol::IcmpEcho) => {
            SocketWorker::<Ipv6, IcmpEcho, C>::spawn(ctx, properties, events)
        }
    }
}

struct SocketWorker<I: Ip, T: Transport<I>, C> {
    ctx: C,
    data: BindingData<I, T>,
    _marker: PhantomData<(I, T)>,
}

struct NewStream {
    stream: fposix_socket::SynchronousDatagramSocketRequestStream,
    flags: fio::OpenFlags,
}

impl<I, T, SC> SocketWorker<I, T, SC>
where
    I: SocketCollectionIpExt<T> + IpExt + IpSockAddrExt,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
    T: TransportState<I>,
    T: BufferTransportState<I, Buf<Vec<u8>>>,
    SC: RequestHandlerContext<I, T>,
    T: Send + Sync + 'static,
    SC: Clone + Send + Sync + 'static,
    DeviceId<StackTime>: TryFromFidlWithContext<<I::SocketAddress as SockAddr>::Zone, Error = DeviceNotFoundError>
        + TryIntoFidlWithContext<<I::SocketAddress as SockAddr>::Zone, Error = DeviceNotFoundError>,
    <SC as RequestHandlerContext<I, T>>::NonSyncCtx: AsRef<Devices<DeviceId<StackTime>>>,
{
    /// Starts servicing events from the provided event stream.
    fn spawn(
        ctx: SC,
        properties: SocketWorkerProperties,
        events: fposix_socket::SynchronousDatagramSocketRequestStream,
    ) -> Result<(), fposix::Errno> {
        let (local_event, peer_event) =
            zx::EventPair::create().map_err(|_| fposix::Errno::Enobufs)?;
        // signal peer that OUTGOING is available.
        // TODO(brunodalbo): We're currently not enforcing any sort of
        // flow-control for outgoing datagrams. That'll get fixed once we
        // limit the number of in flight datagrams per socket (i.e. application
        // buffers).
        if let Err(e) = local_event.signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_OUTGOING) {
            error!("socket failed to signal peer: {:?}", e);
        }
        fasync::Task::spawn(
            async move {
                let data = {
                    let mut guard = ctx.lock().await;
                    let Ctx { sync_ctx, non_sync_ctx } = guard.deref_mut();

                    let unbound_id = T::create_unbound(sync_ctx);
                    let SocketCollection { conns: _, listeners: _ } =
                        I::get_collection_mut(non_sync_ctx);
                    BindingData::new(unbound_id, local_event, peer_event, properties)
                };
                let worker = Self { ctx, data, _marker: PhantomData };

                worker.handle_stream(events).await
            }
            // When the closure above finishes, that means `self` goes out of
            // scope and is dropped, meaning that the event stream's underlying
            // channel is closed. If any errors occurred as a result of the
            // closure, we just log them.
            .unwrap_or_else(|e: fidl::Error| error!("socket control request error: {:?}", e)),
        )
        .detach();
        Ok(())
    }

    /// Handles [a stream of POSIX socket requests].
    ///
    /// Returns when getting the first `Close` request.
    ///
    /// [a stream of POSIX socket requests]: fposix_socket::SynchronousDatagramSocketRequestStream
    async fn handle_stream(
        mut self,
        events: fposix_socket::SynchronousDatagramSocketRequestStream,
    ) -> Result<(), fidl::Error> {
        // Construct the flags for the initial stream.
        let rights = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE;

        // Define `with_flags` out of line so that when futures are pushed into
        // the `OneOrMany`, they all use the same anonymous closure type.
        let with_flags = |flags: fio::OpenFlags| move |x| (x, flags);
        let events = events.into_future().map(with_flags(rights));

        let mut futures = OneOrMany::new(events);
        let deferred_close = loop {
            let Some(((request, request_stream), rights)) = futures.next().await  else {
                // No close request needs to be deferred.
                break None
            };
            let request = match request {
                None => continue,
                Some(Err(e)) => {
                    log::log!(
                        util::fidl_err_log_level(&e),
                        "got error while polling for datagram requests: {}",
                        e
                    );
                    // Continuing implicitly drops the request stream that
                    // produced the error, which would otherwise be re-enqueued
                    // below.
                    continue;
                }
                Some(Ok(t)) => t,
            };
            match RequestHandler::new(&mut self, &rights).handle_request(request).await {
                ControlFlow::Continue(None) => {}
                ControlFlow::Break(close_responder) => {
                    let do_close = move || {
                        responder_send!(close_responder, &mut Ok(()));
                        request_stream.control_handle().shutdown();
                    };
                    if futures.is_empty() {
                        // Save the final close request to be performed
                        // after the socket state is removed from Core.
                        break Some(do_close);
                    }
                    do_close();
                    continue;
                }
                ControlFlow::Continue(Some(NewStream { stream, flags })) => {
                    futures.push(stream.into_future().map(with_flags(flags)))
                }
            };
            futures.push(request_stream.into_future().map(with_flags(rights)));
        };

        self.close_core().await;

        if let Some(do_close) = deferred_close {
            do_close();
        }
        Ok(())
    }

    async fn close_core(self) {
        let Self { ctx, data: BindingData { peer_event: _, info, messages: _ }, _marker } = self;
        let mut ctx = ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
        let SocketControlInfo { _properties, state } = info;
        match state {
            SocketState::Unbound { unbound_id } => {
                T::remove_unbound(sync_ctx, non_sync_ctx, unbound_id)
            }
            SocketState::BoundListen { listener_id } => {
                // Remove from bindings:
                assert_matches!(
                    I::get_collection_mut(non_sync_ctx).listeners.remove(&listener_id),
                    Some(_)
                );
                // Remove from core:
                let _: (Option<ZonedAddr<I::Addr, _>>, T::LocalIdentifier) =
                    T::remove_listener(sync_ctx, non_sync_ctx, listener_id);
            }
            SocketState::BoundConnect { conn_id, .. } => {
                // Remove from bindings:
                assert_matches!(
                    I::get_collection_mut(non_sync_ctx).conns.remove(&conn_id),
                    Some(_)
                );
                // Remove from core:
                let _: (
                    ZonedAddr<I::Addr, _>,
                    T::LocalIdentifier,
                    ZonedAddr<I::Addr, _>,
                    T::RemoteIdentifier,
                ) = T::remove_conn(sync_ctx, non_sync_ctx, conn_id);
            }
        }
    }
}

pub(crate) trait RequestHandlerDispatcher<I, T>:
    AsRef<SocketCollectionPair<T>> + AsMut<SocketCollectionPair<T>>
where
    I: IpExt,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
    T: Transport<I>,
{
}

impl<I, T, D> RequestHandlerDispatcher<I, T> for D
where
    I: IpExt,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
    T: Transport<I>,
    D: AsRef<SocketCollectionPair<T>> + AsMut<SocketCollectionPair<T>>,
{
}

// TODO(https://github.com/rust-lang/rust/issues/20671): Replace the duplicate associated type with
// a where clause bounding the parent trait's associated type.
//
// OR
//
// TODO(https://github.com/rust-lang/rust/issues/52662): Replace the duplicate associated type with
// a bound on the parent trait's associated type.
trait RequestHandlerContext<I, T>:
    LockableContext<NonSyncCtx = <Self as RequestHandlerContext<I, T>>::NonSyncCtx>
where
    I: IpExt,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
    T: Transport<I>,
{
    type NonSyncCtx: RequestHandlerDispatcher<I, T>
        + NonSyncContext<Instant = StackTime>
        + AsRef<Devices<DeviceId<StackTime>>>;
}

impl<I, T, C> RequestHandlerContext<I, T> for C
where
    I: IpExt,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
    T: Transport<I>,
    C: LockableContext,
    C::NonSyncCtx: RequestHandlerDispatcher<I, T> + AsRef<Devices<DeviceId<StackTime>>>,
{
    type NonSyncCtx = C::NonSyncCtx;
}

/// A borrow into a [`SocketWorker`]'s state along with the flags used to handle
/// any requests.
struct RequestHandler<'a, I: Ip, T: Transport<I>, C> {
    ctx: &'a mut C,
    data: &'a mut BindingData<I, T>,
    flags: &'a fio::OpenFlags,
}

impl<'a, I: Ip, T: Transport<I>, SC> RequestHandler<'a, I, T, SC> {
    fn new(worker: &'a mut SocketWorker<I, T, SC>, flags: &'a fio::OpenFlags) -> Self {
        let SocketWorker { ctx, data, _marker } = worker;
        Self { ctx, data, flags }
    }
}

impl<'a, I, T, SC> RequestHandler<'a, I, T, SC>
where
    I: SocketCollectionIpExt<T> + IpExt + IpSockAddrExt,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
    T: TransportState<I>,
    T: BufferTransportState<I, Buf<Vec<u8>>>,
    SC: RequestHandlerContext<I, T>,
    T: Send + Sync + 'static,
    SC: Clone + Send + Sync + 'static,
    DeviceId<StackTime>: TryFromFidlWithContext<<I::SocketAddress as SockAddr>::Zone, Error = DeviceNotFoundError>
        + TryIntoFidlWithContext<<I::SocketAddress as SockAddr>::Zone, Error = DeviceNotFoundError>,
    <SC as RequestHandlerContext<I, T>>::NonSyncCtx: AsRef<Devices<DeviceId<StackTime>>>,
{
    async fn handle_request(
        mut self,
        request: fposix_socket::SynchronousDatagramSocketRequest,
    ) -> ControlFlow<fposix_socket::SynchronousDatagramSocketCloseResponder, Option<NewStream>>
    {
        match request {
            fposix_socket::SynchronousDatagramSocketRequest::DescribeDeprecated { responder } => {
                // If the call to duplicate_handle fails, we have no
                // choice but to drop the responder and close the
                // channel, since Describe must be infallible.
                if let Some(mut info) =
                    self.describe().map(fio::NodeInfoDeprecated::SynchronousDatagramSocket)
                {
                    responder_send!(responder, &mut info);
                }
            }
            fposix_socket::SynchronousDatagramSocketRequest::Describe { responder } => {
                // If the call to duplicate_handle fails, we have no
                // choice but to drop the responder and close the
                // channel, since Describe must be infallible.
                if let Some(fio::SynchronousDatagramSocket { event }) = self.describe() {
                    responder_send!(
                        responder,
                        fposix_socket::SynchronousDatagramSocketDescribeResponse {
                            event: Some(event),
                            ..fposix_socket::SynchronousDatagramSocketDescribeResponse::EMPTY
                        }
                    );
                }
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetConnectionInfo { responder } => {
                let _ = responder;
                todo!("https://fxbug.dev/77623");
            }
            fposix_socket::SynchronousDatagramSocketRequest::Connect { addr, responder } => {
                responder_send!(responder, &mut self.connect(addr).await);
            }
            fposix_socket::SynchronousDatagramSocketRequest::Disconnect { responder } => {
                responder_send!(responder, &mut self.disconnect().await);
            }
            fposix_socket::SynchronousDatagramSocketRequest::Clone { flags, object, .. } => {
                if let Some((stream, flags)) = self.handle_clone(object, flags) {
                    return ControlFlow::Continue(Some(NewStream { stream, flags }));
                }
            }
            fposix_socket::SynchronousDatagramSocketRequest::Clone2 {
                request,
                control_handle: _,
            } => {
                if let Some((stream, flags)) =
                    self.handle_clone(request, fio::OpenFlags::CLONE_SAME_RIGHTS)
                {
                    return ControlFlow::Continue(Some(NewStream { stream, flags }));
                }
            }
            fposix_socket::SynchronousDatagramSocketRequest::Reopen {
                rights_request,
                object_request,
                control_handle: _,
            } => {
                let _ = object_request;
                todo!("https://fxbug.dev/77623: rights_request={:?}", rights_request);
            }
            fposix_socket::SynchronousDatagramSocketRequest::Close { responder } => {
                return ControlFlow::Break(responder);
            }
            fposix_socket::SynchronousDatagramSocketRequest::Sync { responder } => {
                responder_send!(responder, &mut Err(zx::Status::NOT_SUPPORTED.into_raw()));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetAttr { responder } => {
                responder_send!(
                    responder,
                    zx::Status::NOT_SUPPORTED.into_raw(),
                    &mut fio::NodeAttributes {
                        mode: 0,
                        id: 0,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 0,
                        creation_time: 0,
                        modification_time: 0
                    }
                );
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetAttr {
                flags: _,
                attributes: _,
                responder,
            } => {
                responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw());
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetAttributes { query, responder } => {
                let _ = responder;
                todo!("https://fxbug.dev/77623: query={:?}", query);
            }
            fposix_socket::SynchronousDatagramSocketRequest::UpdateAttributes {
                payload,
                responder,
            } => {
                let _ = responder;
                todo!("https://fxbug.dev/77623: payload={:?}", payload);
            }
            fposix_socket::SynchronousDatagramSocketRequest::Bind { addr, responder } => {
                responder_send!(responder, &mut self.bind(addr).await);
            }
            fposix_socket::SynchronousDatagramSocketRequest::Query { responder } => {
                responder_send!(
                    responder,
                    fposix_socket::SYNCHRONOUS_DATAGRAM_SOCKET_PROTOCOL_NAME.as_bytes()
                );
            }
            fposix_socket::SynchronousDatagramSocketRequest::QueryFilesystem { responder } => {
                responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw(), None);
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetSockName { responder } => {
                responder_send!(responder, &mut self.get_sock_name().await);
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetPeerName { responder } => {
                responder_send!(responder, &mut self.get_peer_name().await);
            }
            fposix_socket::SynchronousDatagramSocketRequest::Shutdown { mode, responder } => {
                responder_send!(responder, &mut self.shutdown(mode).await)
            }
            fposix_socket::SynchronousDatagramSocketRequest::RecvMsg {
                want_addr,
                data_len,
                want_control: _,
                flags,
                responder,
            } => {
                // TODO(brunodalbo) handle control
                responder_send!(
                    responder,
                    &mut self.recv_msg(want_addr, data_len as usize, flags).await
                );
            }
            fposix_socket::SynchronousDatagramSocketRequest::SendMsg {
                addr,
                data,
                control: _,
                flags: _,
                responder,
            } => {
                // TODO(https://fxbug.dev/21106): handle control.
                responder_send!(responder, &mut self.send_msg(addr.map(|addr| *addr), data).await);
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetFlags { responder } => {
                responder_send!(
                    responder,
                    zx::Status::NOT_SUPPORTED.into_raw(),
                    fio::OpenFlags::empty()
                );
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetFlags { flags: _, responder } => {
                responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw());
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetInfo { responder } => {
                responder_send!(responder, &mut self.get_sock_info())
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetTimestamp { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetTimestampDeprecated {
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetTimestamp {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetTimestampDeprecated {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetError { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetSendBuffer {
                value_bytes: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetSendBuffer { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetReceiveBuffer {
                value_bytes,
                responder,
            } => {
                responder_send!(responder, &mut {
                    self.set_max_receive_buffer_size(value_bytes);
                    Ok(())
                });
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetReceiveBuffer { responder } => {
                responder_send!(responder, &mut Ok(self.get_max_receive_buffer_size()));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetReuseAddress {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetReuseAddress { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetReusePort { value, responder } => {
                responder_send!(responder, &mut self.set_reuse_port(value).await);
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetReusePort { responder } => {
                responder_send!(responder, &mut Ok(self.get_reuse_port().await));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetAcceptConn { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetBindToDevice {
                value,
                responder,
            } => {
                let identifier = (!value.is_empty()).then_some(value.as_str());
                responder_send!(responder, &mut self.bind_to_device(identifier).await);
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetBindToDevice { responder } => {
                responder_send!(
                    responder,
                    &mut self.get_bound_device().await.map(|d| d.unwrap_or("".to_string()))
                );
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetBroadcast {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetBroadcast { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetKeepAlive {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetKeepAlive { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetLinger {
                linger: _,
                length_secs: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetLinger { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetOutOfBandInline {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetOutOfBandInline { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetNoCheck { value: _, responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetNoCheck { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpv6Only { value, responder } => {
                // TODO(https://fxbug.dev/21198): support dual-stack sockets.
                responder_send!(
                    responder,
                    &mut value.then_some(()).ok_or(fposix::Errno::Eopnotsupp)
                );
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpv6Only { responder } => {
                // TODO(https://fxbug.dev/21198): support dual-stack
                // sockets.
                responder_send!(responder, &mut Ok(true));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpv6TrafficClass {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpv6TrafficClass { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpv6MulticastInterface {
                value: _,
                responder,
            } => {
                error!("TODO(https://fxbug.dev/107644): implement IPV6_MULTICAST_IF socket option");
                responder_send!(responder, &mut Ok(()));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpv6MulticastInterface {
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpv6UnicastHops {
                value,
                responder,
            } => {
                responder_send!(
                    responder,
                    &mut self.set_unicast_hop_limit(Ipv6::VERSION, value).await
                )
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpv6UnicastHops { responder } => {
                responder_send!(responder, &mut self.get_unicast_hop_limit(Ipv6::VERSION).await)
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpv6MulticastHops {
                value,
                responder,
            } => {
                responder_send!(
                    responder,
                    &mut self.set_multicast_hop_limit(Ipv6::VERSION, value).await
                )
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpv6MulticastHops { responder } => {
                responder_send!(responder, &mut self.get_multicast_hop_limit(Ipv6::VERSION).await)
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpv6MulticastLoopback {
                value,
                responder,
            } => {
                // TODO(https://fxbug.dev/106865): add support for
                // looping back sent packets.
                responder_send!(
                    responder,
                    &mut (!value).then_some(()).ok_or(fposix::Errno::Enoprotoopt)
                );
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpv6MulticastLoopback {
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpTtl { value, responder } => {
                responder_send!(
                    responder,
                    &mut self.set_unicast_hop_limit(Ipv4::VERSION, value).await
                )
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpTtl { responder } => {
                responder_send!(responder, &mut self.get_unicast_hop_limit(Ipv4::VERSION).await)
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpMulticastTtl {
                value,
                responder,
            } => {
                responder_send!(
                    responder,
                    &mut self.set_multicast_hop_limit(Ipv4::VERSION, value).await
                )
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpMulticastTtl { responder } => {
                responder_send!(responder, &mut self.get_multicast_hop_limit(Ipv4::VERSION).await)
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpMulticastInterface {
                iface: _,
                address: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpMulticastInterface {
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpMulticastLoopback {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpMulticastLoopback {
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpTypeOfService {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpTypeOfService { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::AddIpMembership {
                membership,
                responder,
            } => {
                responder_send!(responder, &mut {
                    self.set_multicast_membership(membership, true).await
                });
            }
            fposix_socket::SynchronousDatagramSocketRequest::DropIpMembership {
                membership,
                responder,
            } => {
                responder_send!(responder, &mut {
                    self.set_multicast_membership(membership, false).await
                });
            }
            fposix_socket::SynchronousDatagramSocketRequest::AddIpv6Membership {
                membership,
                responder,
            } => {
                responder_send!(responder, &mut {
                    self.set_multicast_membership(membership, true).await
                });
            }
            fposix_socket::SynchronousDatagramSocketRequest::DropIpv6Membership {
                membership,
                responder,
            } => {
                responder_send!(responder, &mut {
                    self.set_multicast_membership(membership, false).await
                });
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpv6ReceiveTrafficClass {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpv6ReceiveTrafficClass {
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpv6ReceiveHopLimit {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpv6ReceiveHopLimit {
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpReceiveTypeOfService {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpReceiveTypeOfService {
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpv6ReceivePacketInfo {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpv6ReceivePacketInfo {
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpReceiveTtl {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpReceiveTtl { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::SetIpPacketInfo {
                value: _,
                responder,
            } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::SynchronousDatagramSocketRequest::GetIpPacketInfo { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
        }
        ControlFlow::Continue(None)
    }

    /// Returns the intersection of the requested flags against the current
    /// rights.
    ///
    /// If the requested flags contains invalid flags, or requests rights that
    /// aren't held by `self.flags`, returns `None`. Otherwise returns the new
    /// flags.
    fn new_rights(&self, request_flags: fio::OpenFlags) -> Option<fio::OpenFlags> {
        // Datagram sockets don't understand the following flags.
        if request_flags.intersects(fio::OpenFlags::APPEND) {
            return None;
        }
        // Datagram sockets are neither mountable nor executable.
        if request_flags.intersects(fio::OpenFlags::RIGHT_EXECUTABLE) {
            return None;
        }
        // Cannot specify CLONE_FLAGS_SAME_RIGHTS together with
        // OPEN_RIGHT_* flags.
        if request_flags.intersects(fio::OpenFlags::CLONE_SAME_RIGHTS)
            && (request_flags.intersects(fio::OpenFlags::RIGHT_READABLE)
                || request_flags.intersects(fio::OpenFlags::RIGHT_WRITABLE))
        {
            return None;
        }
        // If CLONE_SAME_RIGHTS is not set, then use the intersection of the
        // inherited rights and the newly specified rights.
        if request_flags.intersects(fio::OpenFlags::CLONE_SAME_RIGHTS) {
            return Some(self.flags.clone());
        }

        // Check that the new rights are a subset of the current rights.
        let new_rights =
            request_flags & (fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);
        if !self.flags.contains(new_rights) {
            return None;
        }

        Some(self.flags.clone() & new_rights)
    }

    fn handle_clone(
        self,
        request: ServerEnd<impl super::CanClone>,
        requested_flags: fio::OpenFlags,
    ) -> Option<(fposix_socket::SynchronousDatagramSocketRequestStream, fio::OpenFlags)> {
        let channel = fidl::AsyncChannel::from_channel(request.into_channel())
            .expect("failed to create async channel");
        let request_stream =
            fposix_socket::SynchronousDatagramSocketRequestStream::from_channel(channel);
        let control_handle = request_stream.control_handle();
        let send_on_open = |status: i32, info: Option<&mut fio::NodeInfoDeprecated>| {
            let () = control_handle.send_on_open_(status, info).unwrap_or_else(|e| {
                log::log!(
                    util::fidl_err_log_level(&e),
                    "failed to send OnOpen event with status ({}): {}",
                    status,
                    e
                )
            });
        };

        match self.new_rights(requested_flags) {
            None => {
                send_on_open(zx::sys::ZX_ERR_INVALID_ARGS, None);
                None
            }
            Some(flags) => {
                if requested_flags.intersects(fio::OpenFlags::DESCRIBE) {
                    let mut info =
                        self.describe().map(fio::NodeInfoDeprecated::SynchronousDatagramSocket);
                    send_on_open(zx::sys::ZX_OK, info.as_mut());
                }
                Some((request_stream, flags))
            }
        }
    }

    fn describe(&self) -> Option<fio::SynchronousDatagramSocket> {
        let Self { ctx: _, data: BindingData { peer_event, info: _, messages: _ }, flags: _ } =
            self;
        peer_event
            .duplicate_handle(zx::Rights::BASIC)
            .map(|peer| fio::SynchronousDatagramSocket { event: peer })
            .ok()
    }

    fn get_max_receive_buffer_size(&self) -> u64 {
        let Self { ctx: _, data: BindingData { peer_event: _, info: _, messages }, flags: _ } =
            self;
        messages.lock().queue.max_available_messages_size.try_into().unwrap_or(u64::MAX)
    }

    fn set_max_receive_buffer_size(&mut self, max_bytes: u64) {
        let max_bytes = max_bytes
            .try_into()
            .ok_checked::<TryFromIntError>()
            .unwrap_or(MAX_OUTSTANDING_APPLICATION_MESSAGES_SIZE);
        let max_bytes = std::cmp::min(
            std::cmp::max(max_bytes, MIN_OUTSTANDING_APPLICATION_MESSAGES_SIZE),
            MAX_OUTSTANDING_APPLICATION_MESSAGES_SIZE,
        );
        let Self { ctx: _, data: BindingData { peer_event: _, info: _, messages }, flags: _ } =
            self;
        messages.lock().queue.max_available_messages_size = max_bytes
    }

    /// Handles a [POSIX socket connect request].
    ///
    /// [POSIX socket connect request]: fposix_socket::SynchronousDatagramSocketRequest::Connect
    async fn connect(self, addr: fnet::SocketAddress) -> Result<(), fposix::Errno> {
        let mut ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
        let sockaddr = I::SocketAddress::from_sock_addr(addr)?;
        trace!("connect sockaddr: {:?}", sockaddr);
        let (remote_addr, remote_port) =
            sockaddr.try_into_core_with_ctx(&non_sync_ctx).map_err(IntoErrno::into_errno)?;
        let remote_port =
            T::RemoteIdentifier::from_u16(remote_port).ok_or(fposix::Errno::Econnrefused)?;
        // Emulate Linux, which was emulating BSD, by treating the unspecified
        // remote address as localhost.
        let remote_addr = remote_addr.unwrap_or(ZonedAddr::Unzoned(I::LOOPBACK_ADDRESS));

        let (conn_id, messages) = match self.data.info.state {
            SocketState::Unbound { unbound_id } => {
                // Use None for local_addr and local_port.
                let conn_id = T::connect_unbound(
                    sync_ctx,
                    non_sync_ctx,
                    unbound_id,
                    remote_addr,
                    remote_port,
                )
                .map_err(IntoErrno::into_errno)?;
                (conn_id, self.data.messages.clone())
            }
            SocketState::BoundListen { listener_id } => {
                // Whether connect_listener succeeds or fails, it will consume
                // the existing listener.
                // TODO(https://fxbug.dev/103049): Make T::connect_listener not
                // remove the existing listener on failure.
                let messages = assert_matches!(
                    I::get_collection_mut(non_sync_ctx).listeners.remove(&listener_id),
                    Some(t) => t
                );

                match T::connect_listener(
                    sync_ctx,
                    non_sync_ctx,
                    listener_id,
                    remote_addr,
                    remote_port,
                ) {
                    Ok(conn_id) => (conn_id, messages),
                    Err((e, listener_id)) => {
                        // Replace the consumed listener with the new one.
                        assert_matches!(
                            I::get_collection_mut(non_sync_ctx)
                                .listeners
                                .insert(&listener_id, messages),
                            None
                        );
                        self.data.info.state = SocketState::BoundListen { listener_id };
                        return Err(e.into_errno());
                    }
                }
            }
            SocketState::BoundConnect { conn_id, shutdown_read, shutdown_write } => {
                // if we're bound to a connect mode, we need to remove the
                // connection, and retrieve the bound local addr and port.

                // Whether reconnect_conn succeeds or fails, it will consume
                // the existing socket.
                let messages = assert_matches!(
                    I::get_collection_mut(non_sync_ctx).conns.remove(&conn_id),
                    Some(t) => t);

                match T::reconnect_conn(sync_ctx, non_sync_ctx, conn_id, remote_addr, remote_port) {
                    Ok(conn_id) => (conn_id, messages),
                    Err((e, conn_id)) => {
                        assert_matches!(
                            I::get_collection_mut(non_sync_ctx).conns.insert(&conn_id, messages),
                            None
                        );
                        self.data.info.state =
                            SocketState::BoundConnect { conn_id, shutdown_read, shutdown_write };
                        return Err(e.into_errno());
                    }
                }
            }
        };

        self.data.info.state =
            SocketState::BoundConnect { conn_id, shutdown_read: false, shutdown_write: false };
        assert_matches!(I::get_collection_mut(non_sync_ctx).conns.insert(&conn_id, messages), None);
        Ok(())
    }

    /// Handles a [POSIX socket bind request].
    ///
    /// [POSIX socket bind request]: fposix_socket::SynchronousDatagramSocketRequest::Bind
    async fn bind(self, addr: fnet::SocketAddress) -> Result<(), fposix::Errno> {
        let sockaddr = I::SocketAddress::from_sock_addr(addr)?;
        trace!("bind sockaddr: {:?}", sockaddr);

        let mut ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();

        let (sockaddr, port) =
            TryFromFidlWithContext::try_from_fidl_with_ctx(&non_sync_ctx, sockaddr)
                .map_err(IntoErrno::into_errno)?;
        let unbound_id = match self.data.info.state {
            SocketState::Unbound { unbound_id } => Ok(unbound_id),
            SocketState::BoundListen { listener_id: _ }
            | SocketState::BoundConnect { conn_id: _, shutdown_read: _, shutdown_write: _ } => {
                Err(fposix::Errno::Einval)
            }
        }?;
        let listener_id = Self::bind_inner(
            sync_ctx,
            non_sync_ctx,
            unbound_id,
            &self.data.messages,
            sockaddr,
            T::LocalIdentifier::from_u16(port),
        )?;
        self.data.info.state = SocketState::BoundListen { listener_id };
        Ok(())
    }

    /// Helper function for common functionality to self.bind() and self.send_msg().
    fn bind_inner(
        sync_ctx: &SyncCtx<<SC as RequestHandlerContext<I, T>>::NonSyncCtx>,
        non_sync_ctx: &mut <SC as RequestHandlerContext<I, T>>::NonSyncCtx,
        unbound_id: <T as Transport<I>>::UnboundId,
        available_data: &Arc<Mutex<MessageQueue<I, T>>>,
        local_addr: Option<ZonedAddr<I::Addr, DeviceId<StackTime>>>,
        local_port: Option<<T as TransportState<I>>::LocalIdentifier>,
    ) -> Result<<T as Transport<I>>::ListenerId, fposix::Errno> {
        let listener_id =
            T::listen_on_unbound(sync_ctx, non_sync_ctx, unbound_id, local_addr, local_port)
                .map_err(IntoErrno::into_errno)?;
        assert_matches!(
            I::get_collection_mut(non_sync_ctx)
                .listeners
                .insert(&listener_id, available_data.clone()),
            None
        );
        Ok(listener_id)
    }

    /// Handles a [POSIX socket disconnect request].
    ///
    /// [POSIX socket connect request]: fposix_socket::SynchronousDatagramSocketRequest::Disconnect
    async fn disconnect(self) -> Result<(), fposix::Errno> {
        trace!("disconnect socket");

        let mut ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();

        let (listener_id, messages) = match self.data.info.state {
            SocketState::Unbound { unbound_id: _ }
            | SocketState::BoundListen { listener_id: _ } => return Err(fposix::Errno::Einval),
            SocketState::BoundConnect { conn_id, .. } => {
                let messages = assert_matches!(
                    I::get_collection_mut(non_sync_ctx).conns.remove(&conn_id),
                    Some(t) => t);
                let listener_id = T::disconnect_connected(sync_ctx, non_sync_ctx, conn_id);
                (listener_id, messages)
            }
        };

        self.data.info.state = SocketState::BoundListen { listener_id };
        assert_matches!(
            I::get_collection_mut(non_sync_ctx).listeners.insert(&listener_id, messages),
            None
        );
        Ok(())
    }

    /// Handles a [POSIX socket get_sock_name request].
    ///
    /// [POSIX socket get_sock_name request]: fposix_socket::SynchronousDatagramSocketRequest::GetSockName
    async fn get_sock_name(self) -> Result<fnet::SocketAddress, fposix::Errno> {
        let mut ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
        match self.data.info.state {
            SocketState::Unbound { .. } => {
                return Err(fposix::Errno::Enotsock);
            }
            SocketState::BoundConnect { conn_id, .. } => {
                let (local_ip, local_port, _, _): (
                    _,
                    _,
                    ZonedAddr<I::Addr, _>,
                    T::RemoteIdentifier,
                ) = T::get_conn_info(sync_ctx, non_sync_ctx, conn_id);
                (Some(local_ip), local_port.into()).try_into_fidl_with_ctx(&non_sync_ctx)
            }
            SocketState::BoundListen { listener_id } => {
                let (local_ip, local_port) =
                    T::get_listener_info(sync_ctx, non_sync_ctx, listener_id);
                (local_ip, local_port.into()).try_into_fidl_with_ctx(&non_sync_ctx)
            }
        }
        .map(SockAddr::into_sock_addr)
        .map_err(IntoErrno::into_errno)
    }

    /// Handles a [POSIX socket get_info request].
    ///
    /// [POSIX socket get_info request]: fposix_socket::SynchronousDatagramSocketRequest::GetInfo
    fn get_sock_info(
        self,
    ) -> Result<(fposix_socket::Domain, fposix_socket::DatagramSocketProtocol), fposix::Errno> {
        let domain = match I::VERSION {
            IpVersion::V4 => fposix_socket::Domain::Ipv4,
            IpVersion::V6 => fposix_socket::Domain::Ipv6,
        };
        let protocol = match <T as Transport<I>>::PROTOCOL {
            DatagramProtocol::Udp => fposix_socket::DatagramSocketProtocol::Udp,
            DatagramProtocol::IcmpEcho => fposix_socket::DatagramSocketProtocol::IcmpEcho,
        };

        Ok((domain, protocol))
    }

    /// Handles a [POSIX socket get_peer_name request].
    ///
    /// [POSIX socket get_peer_name request]: fposix_socket::SynchronousDatagramSocketRequest::GetPeerName
    async fn get_peer_name(self) -> Result<fnet::SocketAddress, fposix::Errno> {
        let mut ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
        match self.data.info.state {
            SocketState::Unbound { .. } => {
                return Err(fposix::Errno::Enotsock);
            }
            SocketState::BoundListen { .. } => {
                return Err(fposix::Errno::Enotconn);
            }
            SocketState::BoundConnect { conn_id, .. } => {
                let (_, _, remote_ip, remote_port): (
                    ZonedAddr<I::Addr, _>,
                    T::LocalIdentifier,
                    _,
                    _,
                ) = T::get_conn_info(sync_ctx, non_sync_ctx, conn_id);
                (Some(remote_ip), remote_port.into())
                    .try_into_fidl_with_ctx(&non_sync_ctx)
                    .map(SockAddr::into_sock_addr)
                    .map_err(IntoErrno::into_errno)
            }
        }
    }

    fn need_rights(&self, required: fio::OpenFlags) -> Result<(), fposix::Errno> {
        if *self.flags & required == required {
            Ok(())
        } else {
            Err(fposix::Errno::Eperm)
        }
    }

    async fn recv_msg(
        self,
        want_addr: bool,
        data_len: usize,
        recv_flags: fposix_socket::RecvMsgFlags,
    ) -> Result<
        (
            Option<Box<fnet::SocketAddress>>,
            Vec<u8>,
            fposix_socket::DatagramSocketRecvControlData,
            u32,
        ),
        fposix::Errno,
    > {
        let () = self.need_rights(fio::OpenFlags::RIGHT_READABLE)?;
        let Self { ctx: _, data: BindingData { peer_event: _, info, messages }, flags: _ } = self;
        let mut messages = messages.lock();
        let front = if recv_flags.contains(fposix_socket::RecvMsgFlags::PEEK) {
            messages.peek().cloned()
        } else {
            messages.pop()
        };
        let available = if let Some(front) = front {
            front
        } else {
            if let SocketState::BoundConnect { shutdown_read, .. } = info.state {
                if shutdown_read {
                    // Return empty data to signal EOF.
                    return Ok((
                        None,
                        Vec::new(),
                        fposix_socket::DatagramSocketRecvControlData::EMPTY,
                        0,
                    ));
                }
            }
            return Err(fposix::Errno::Eagain);
        };
        let addr = if want_addr {
            Some(Box::new(
                I::SocketAddress::new(
                    SpecifiedAddr::new(available.source_addr).map(ZonedAddr::Unzoned),
                    available.source_port,
                )
                .into_sock_addr(),
            ))
        } else {
            None
        };
        let mut data = available.data;
        let truncated = data.len().saturating_sub(data_len);
        data.truncate(data_len);

        Ok((
            addr,
            data,
            fposix_socket::DatagramSocketRecvControlData::EMPTY,
            truncated.try_into().unwrap_or(u32::MAX),
        ))
    }

    async fn send_msg(
        self,
        addr: Option<fnet::SocketAddress>,
        data: Vec<u8>,
    ) -> Result<i64, fposix::Errno> {
        let () = self.need_rights(fio::OpenFlags::RIGHT_WRITABLE)?;
        let remote = if let Some(addr) = addr {
            let sockaddr = I::SocketAddress::from_sock_addr(addr)?;
            let addr = sockaddr.get_specified_addr().ok_or(fposix::Errno::Einval)?;
            let port =
                T::RemoteIdentifier::from_u16(sockaddr.port()).ok_or(fposix::Errno::Einval)?;
            Some((addr, port))
        } else {
            None
        };
        let len = data.len() as i64;
        let body = Buf::new(data, ..);
        let mut ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
        match self.data.info.state {
            SocketState::Unbound { unbound_id } => match remote {
                Some((addr, port)) => {
                    // On Linux, sending on an unbound socket is equivalent to
                    // first binding to a system-selected port for all IPs, then
                    // sending from that socket. Emulate that here by binding
                    // with an unspecified IP and port.
                    Self::bind_inner(
                        sync_ctx,
                        non_sync_ctx,
                        unbound_id,
                        &self.data.messages,
                        None,
                        None,
                    )
                    .and_then(|listener_id| {
                        self.data.info.state = SocketState::BoundListen { listener_id };
                        T::send_listener(
                            sync_ctx,
                            non_sync_ctx,
                            listener_id,
                            None,
                            addr,
                            port,
                            body,
                        )
                        .map_err(|(_body, err)| err.into_errno())
                    })
                }
                None => Err(fposix::Errno::Edestaddrreq),
            },
            SocketState::BoundConnect { conn_id, shutdown_write, .. } => {
                if shutdown_write {
                    return Err(fposix::Errno::Epipe);
                }
                T::send_conn(sync_ctx, non_sync_ctx, conn_id, body, remote)
                    .map_err(|(_body, err)| err.into_errno())
            }
            SocketState::BoundListen { listener_id } => match remote {
                Some((addr, port)) => {
                    T::send_listener(sync_ctx, non_sync_ctx, listener_id, None, addr, port, body)
                        .map_err(|(_body, err)| err.into_errno())
                }
                None => Err(fposix::Errno::Edestaddrreq),
            },
        }
        .map(|()| len)
    }

    async fn bind_to_device(self, device: Option<&str>) -> Result<(), fposix::Errno> {
        let mut ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
        let device = device
            .map(|name| {
                non_sync_ctx
                    .as_ref()
                    .get_device_by_name(name)
                    .map(|d| d.core_id().clone())
                    .ok_or(fposix::Errno::Enodev)
            })
            .transpose()?;
        let state: &SocketState<_, _> = &self.data.info.state;
        let id = state.into();

        T::set_socket_device(sync_ctx, non_sync_ctx, id, device.as_ref())
            .map_err(IntoErrno::into_errno)
    }

    async fn get_bound_device(self) -> Result<Option<String>, fposix::Errno> {
        let ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref();
        let state: &SocketState<_, _> = &self.data.info.state;
        let id = state.into();

        let device = match T::get_bound_device(sync_ctx, non_sync_ctx, id) {
            None => return Ok(None),
            Some(d) => d,
        };
        let index = TryIntoFidlWithContext::<u64>::try_into_fidl_with_ctx(device, &non_sync_ctx)
            .map_err(IntoErrno::into_errno)?;
        Ok(non_sync_ctx.as_ref().get_device(index).map(|device_info| {
            let CommonInfo {
                name,
                mtu: _,
                admin_enabled: _,
                events: _,
                control_hook: _,
                addresses: _,
            } = device_info.info().common_info();
            name.to_string()
        }))
    }

    async fn set_reuse_port(self, reuse_port: bool) -> Result<(), fposix::Errno> {
        let mut ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
        match self.data.info.state {
            SocketState::Unbound { unbound_id } => {
                T::set_reuse_port(sync_ctx, non_sync_ctx, unbound_id, reuse_port);
                Ok(())
            }
            SocketState::BoundListen { .. } | SocketState::BoundConnect { .. } => {
                warn!("tried to set SO_REUSEPORT on a bound socket; see https://fxbug.dev/100840");
                Err(fposix::Errno::Eopnotsupp)
            }
        }
    }

    async fn get_reuse_port(self) -> bool {
        let ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref();

        T::get_reuse_port(sync_ctx, non_sync_ctx, (&self.data.info.state).into())
    }

    async fn shutdown(self, how: fposix_socket::ShutdownMode) -> Result<(), fposix::Errno> {
        let Self { data: BindingData { peer_event: _, info, messages }, ctx: _, flags: _ } = self;

        // Only "connected" sockets can be shutdown.
        if let SocketState::BoundConnect { ref mut shutdown_read, ref mut shutdown_write, .. } =
            info.state
        {
            if how.is_empty() {
                return Err(fposix::Errno::Einval);
            }
            // Shutting down a socket twice is valid so we can just blindly set
            // the corresponding flags.
            if how.contains(fposix_socket::ShutdownMode::WRITE) {
                *shutdown_write = true;
            }
            if how.contains(fposix_socket::ShutdownMode::READ) {
                *shutdown_read = true;
                if let Err(e) = messages
                    .lock()
                    .local_event
                    .signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_INCOMING)
                {
                    error!("Failed to signal peer when shutting down: {:?}", e);
                }
            }
            return Ok(());
        }
        Err(fposix::Errno::Enotconn)
    }

    async fn set_multicast_membership<
        M: TryIntoCore<(
            MulticastAddr<I::Addr>,
            Option<MulticastInterfaceSelector<I::Addr, NonZeroU64>>,
        )>,
    >(
        self,
        membership: M,
        want_membership: bool,
    ) -> Result<(), fposix::Errno>
    where
        M::Error: IntoErrno,
    {
        let (multicast_group, interface) =
            membership.try_into_core().map_err(IntoErrno::into_errno)?;
        let interface = interface
            .map_or(MulticastMembershipInterfaceSelector::AnyInterfaceWithRoute, Into::into);

        let id = (&self.data.info.state).into();

        let mut ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();

        let interface =
            interface.try_into_core_with_ctx(non_sync_ctx).map_err(IntoErrno::into_errno)?;

        T::set_multicast_membership(
            sync_ctx,
            non_sync_ctx,
            id,
            multicast_group,
            interface,
            want_membership,
        )
        .map_err(IntoErrno::into_errno)
    }

    async fn set_unicast_hop_limit(
        self,
        ip_version: IpVersion,
        hop_limit: fposix_socket::OptionalUint8,
    ) -> Result<(), fposix::Errno> {
        // TODO(https://fxbug.dev/21198): Allow setting hop limits for
        // dual-stack sockets.
        if ip_version != I::VERSION {
            return Err(fposix::Errno::Enoprotoopt);
        }

        let id = (&self.data.info.state).into();

        let hop_limit: Option<u8> = hop_limit.into_core();
        let hop_limit =
            hop_limit.map(|u| NonZeroU8::new(u).ok_or(fposix::Errno::Einval)).transpose()?;

        let mut ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
        T::set_unicast_hop_limit(sync_ctx, non_sync_ctx, id, hop_limit);
        Ok(())
    }

    async fn set_multicast_hop_limit(
        self,
        ip_version: IpVersion,
        hop_limit: fposix_socket::OptionalUint8,
    ) -> Result<(), fposix::Errno> {
        // TODO(https://fxbug.dev/21198): Allow setting hop limits for
        // dual-stack sockets.
        if ip_version != I::VERSION {
            return Err(fposix::Errno::Enoprotoopt);
        }

        let id = (&self.data.info.state).into();

        let hop_limit: Option<u8> = hop_limit.into_core();
        // TODO(https://fxbug.dev/108323): Support setting a multicast hop limit
        // of 0.
        let hop_limit =
            hop_limit.map(|u| NonZeroU8::new(u).ok_or(fposix::Errno::Einval)).transpose()?;

        let mut ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
        T::set_multicast_hop_limit(sync_ctx, non_sync_ctx, id, hop_limit);
        Ok(())
    }

    async fn get_unicast_hop_limit(self, ip_version: IpVersion) -> Result<u8, fposix::Errno> {
        // TODO(https://fxbug.dev/21198): Allow reading hop limits for
        // dual-stack sockets.
        if ip_version != I::VERSION {
            return Err(fposix::Errno::Enoprotoopt);
        }

        let id = (&self.data.info.state).into();

        let ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref();
        Ok(T::get_unicast_hop_limit(sync_ctx, non_sync_ctx, id).get())
    }

    async fn get_multicast_hop_limit(self, ip_version: IpVersion) -> Result<u8, fposix::Errno> {
        // TODO(https://fxbug.dev/21198): Allow reading hop limits for
        // dual-stack sockets.
        if ip_version != I::VERSION {
            return Err(fposix::Errno::Enoprotoopt);
        }

        let id = (&self.data.info.state).into();

        let ctx = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref();

        Ok(T::get_multicast_hop_limit(sync_ctx, non_sync_ctx, id).get())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use anyhow::Error;
    use fidl::{
        encoding::Decodable,
        endpoints::{Proxy, ServerEnd},
    };
    use fuchsia_async as fasync;
    use fuchsia_zircon::{self as zx, AsHandleRef};
    use futures::StreamExt;

    use crate::bindings::socket::testutil::TestSockAddr;
    use crate::bindings::{
        integration_tests::{
            test_ep_name, StackSetupBuilder, TestSetup, TestSetupBuilder, TestStack,
        },
        util::IntoFidl,
    };
    use net_types::{
        ip::{Ip, IpAddr, IpAddress},
        Witness as _,
    };

    async fn prepare_test<A: TestSockAddr>(
        proto: fposix_socket::DatagramSocketProtocol,
    ) -> (TestSetup, fposix_socket::SynchronousDatagramSocketProxy, zx::EventPair) {
        let mut t = TestSetupBuilder::new()
            .add_endpoint()
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint(test_ep_name(1), Some(A::config_addr_subnet())),
            )
            .build()
            .await
            .unwrap();
        let (proxy, event) = get_socket_and_event::<A>(t.get(0), proto).await;
        (t, proxy, event)
    }

    async fn get_socket<A: TestSockAddr>(
        test_stack: &mut TestStack,
        proto: fposix_socket::DatagramSocketProtocol,
    ) -> fposix_socket::SynchronousDatagramSocketProxy {
        let socket_provider = test_stack.connect_socket_provider().unwrap();
        let response = socket_provider
            .datagram_socket(A::DOMAIN, proto)
            .await
            .unwrap()
            .expect("Socket succeeds");
        match response {
            fposix_socket::ProviderDatagramSocketResponse::SynchronousDatagramSocket(sock) => {
                fposix_socket::SynchronousDatagramSocketProxy::new(
                    fasync::Channel::from_channel(sock.into_channel()).unwrap(),
                )
            }
            // TODO(https://fxrev.dev/99905): Implement Fast UDP sockets in Netstack3.
            fposix_socket::ProviderDatagramSocketResponse::DatagramSocket(sock) => {
                let _: fidl::endpoints::ClientEnd<fposix_socket::DatagramSocketMarker> = sock;
                panic!("expected SynchronousDatagramSocket, found DatagramSocket")
            }
        }
    }

    async fn get_socket_and_event<A: TestSockAddr>(
        test_stack: &mut TestStack,
        proto: fposix_socket::DatagramSocketProtocol,
    ) -> (fposix_socket::SynchronousDatagramSocketProxy, zx::EventPair) {
        let ctlr = get_socket::<A>(test_stack, proto).await;
        let fposix_socket::SynchronousDatagramSocketDescribeResponse { event, .. } =
            ctlr.describe().await.expect("Socket describe succeeds");
        (ctlr, event.expect("Socket describe contains event"))
    }

    macro_rules! declare_tests {
        ($test_fn:ident, icmp $(#[$icmp_attributes:meta])*) => {
            mod $test_fn {
                use super::*;

                #[fasync::run_singlethreaded(test)]
                async fn udp_v4() {
                    $test_fn::<fnet::Ipv4SocketAddress, Udp>(
                        fposix_socket::DatagramSocketProtocol::Udp,
                    )
                    .await
                }

                #[fasync::run_singlethreaded(test)]
                async fn udp_v6() {
                    $test_fn::<fnet::Ipv6SocketAddress, Udp>(
                        fposix_socket::DatagramSocketProtocol::Udp,
                    )
                    .await
                }

                #[fasync::run_singlethreaded(test)]
                $(#[$icmp_attributes])*
                async fn icmp_echo_v4() {
                    $test_fn::<fnet::Ipv4SocketAddress, IcmpEcho>(
                        fposix_socket::DatagramSocketProtocol::IcmpEcho,
                    )
                    .await
                }

                #[fasync::run_singlethreaded(test)]
                $(#[$icmp_attributes])*
                async fn icmp_echo_v6() {
                    $test_fn::<fnet::Ipv6SocketAddress, IcmpEcho>(
                        fposix_socket::DatagramSocketProtocol::IcmpEcho,
                    )
                    .await
                }
            }
        };
        ($test_fn:ident) => {
            declare_tests!($test_fn, icmp);
        };
    }

    async fn connect_failure<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol) {
        let (_t, proxy, _event) = prepare_test::<A>(proto).await;

        // Pass a bad domain.
        let res = proxy
            .connect(&mut A::DifferentDomain::create(A::DifferentDomain::LOCAL_ADDR, 1010))
            .await
            .unwrap()
            .expect_err("connect fails");
        assert_eq!(res, fposix::Errno::Eafnosupport);

        // Pass a zero port. UDP disallows it, ICMP allows it.
        let res = proxy.connect(&mut A::create(A::LOCAL_ADDR, 0)).await.unwrap();
        match proto {
            fposix_socket::DatagramSocketProtocol::Udp => {
                assert_eq!(res, Err(fposix::Errno::Econnrefused));
            }
            fposix_socket::DatagramSocketProtocol::IcmpEcho => {
                assert_eq!(res, Ok(()));
            }
        };

        // Pass an unreachable address (tests error forwarding from `create_connection`).
        let res = proxy
            .connect(&mut A::create(A::UNREACHABLE_ADDR, 1010))
            .await
            .unwrap()
            .expect_err("connect fails");
        assert_eq!(res, fposix::Errno::Enetunreach);
    }

    declare_tests!(
        connect_failure,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    async fn connect<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol) {
        let (_t, proxy, _event) = prepare_test::<A>(proto).await;
        let () = proxy
            .connect(&mut A::create(A::REMOTE_ADDR, 200))
            .await
            .unwrap()
            .expect("connect succeeds");

        // Can connect again to a different remote should succeed.
        let () = proxy
            .connect(&mut A::create(A::REMOTE_ADDR_2, 200))
            .await
            .unwrap()
            .expect("connect suceeds");
    }

    declare_tests!(
        connect,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    async fn connect_loopback<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol) {
        let (_t, proxy, _event) = prepare_test::<A>(proto).await;
        let () = proxy
            .connect(&mut A::create(
                <<A::AddrType as IpAddress>::Version as Ip>::LOOPBACK_ADDRESS.get(),
                200,
            ))
            .await
            .unwrap()
            .expect("connect succeeds");
    }

    declare_tests!(connect_loopback);

    async fn connect_any<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol) {
        // Pass an unspecified remote address. This should be treated as the
        // loopback address.
        let (_t, proxy, _event) = prepare_test::<A>(proto).await;

        const PORT: u16 = 1010;
        let () = proxy
            .connect(&mut A::create(<A::AddrType as IpAddress>::Version::UNSPECIFIED_ADDRESS, PORT))
            .await
            .unwrap()
            .unwrap();

        assert_eq!(
            proxy.get_peer_name().await.unwrap().unwrap(),
            A::create(<A::AddrType as IpAddress>::Version::LOOPBACK_ADDRESS.get(), PORT)
        );
    }

    declare_tests!(
        connect_any,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    async fn bind<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol) {
        let (mut t, socket, _event) = prepare_test::<A>(proto).await;
        let stack = t.get(0);
        // Can bind to local address.
        let () =
            socket.bind(&mut A::create(A::LOCAL_ADDR, 200)).await.unwrap().expect("bind succeeds");

        // Can't bind again (to another port).
        let res =
            socket.bind(&mut A::create(A::LOCAL_ADDR, 201)).await.unwrap().expect_err("bind fails");
        assert_eq!(res, fposix::Errno::Einval);

        // Can bind another socket to a different port.
        let socket = get_socket::<A>(stack, proto).await;
        let () =
            socket.bind(&mut A::create(A::LOCAL_ADDR, 201)).await.unwrap().expect("bind succeeds");

        // Can bind to unspecified address in a different port.
        let socket = get_socket::<A>(stack, proto).await;
        let () = socket
            .bind(&mut A::create(<A::AddrType as IpAddress>::Version::UNSPECIFIED_ADDRESS, 202))
            .await
            .unwrap()
            .expect("bind succeeds");
    }

    declare_tests!(bind,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    async fn bind_then_connect<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol) {
        let (_t, socket, _event) = prepare_test::<A>(proto).await;
        // Can bind to local address.
        let () =
            socket.bind(&mut A::create(A::LOCAL_ADDR, 200)).await.unwrap().expect("bind suceeds");

        let () = socket
            .connect(&mut A::create(A::REMOTE_ADDR, 1010))
            .await
            .unwrap()
            .expect("connect succeeds");
    }

    declare_tests!(
        bind_then_connect,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    async fn connect_then_disconnect<A: TestSockAddr, T>(
        proto: fposix_socket::DatagramSocketProtocol,
    ) {
        let (_t, socket, _event) = prepare_test::<A>(proto).await;

        let remote_addr = A::create(A::REMOTE_ADDR, 1010);
        let () = socket.connect(&mut remote_addr.clone()).await.unwrap().expect("connect succeeds");

        assert_eq!(
            socket.get_peer_name().await.unwrap().expect("get_peer_name should suceed"),
            remote_addr
        );
        let () = socket.disconnect().await.unwrap().expect("disconnect succeeds");

        assert_eq!(
            socket.get_peer_name().await.unwrap().expect_err("alice getpeername fails"),
            fposix::Errno::Enotconn
        );
    }

    declare_tests!(connect_then_disconnect,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    /// Tests a simple UDP setup with a client and a server, where the client
    /// can send data to the server and the server receives it.
    // TODO(https://fxbug.dev/47321): this test is incorrect for ICMP sockets. At the time of this
    // writing it crashes before reaching the wrong parts, but we will need to specialize the body
    // of this test for ICMP before calling the feature complete.
    async fn hello<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol) {
        // We create two stacks, Alice (server listening on LOCAL_ADDR:200), and
        // Bob (client, bound on REMOTE_ADDR:300). After setup, Bob connects to
        // Alice and sends a datagram. Finally, we verify that Alice receives
        // the datagram.
        let mut t = TestSetupBuilder::new()
            .add_endpoint()
            .add_endpoint()
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint(test_ep_name(1), Some(A::config_addr_subnet())),
            )
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint(test_ep_name(2), Some(A::config_addr_subnet_remote())),
            )
            .build()
            .await
            .unwrap();
        let alice = t.get(0);
        let (alice_socket, alice_events) = get_socket_and_event::<A>(alice, proto).await;

        // Verify that Alice has no local or peer addresses bound
        assert_eq!(
            alice_socket.get_sock_name().await.unwrap().expect_err("alice getsockname fails"),
            fposix::Errno::Enotsock
        );
        assert_eq!(
            alice_socket.get_peer_name().await.unwrap().expect_err("alice getpeername fails"),
            fposix::Errno::Enotsock
        );

        // Setup Alice as a server, bound to LOCAL_ADDR:200
        println!("Configuring alice...");
        let () = alice_socket
            .bind(&mut A::create(A::LOCAL_ADDR, 200))
            .await
            .unwrap()
            .expect("alice bind suceeds");

        // Verify that Alice is listening on the local socket, but still has no
        // peer socket
        assert_eq!(
            alice_socket.get_sock_name().await.unwrap().expect("alice getsockname succeeds"),
            A::create(A::LOCAL_ADDR, 200)
        );
        assert_eq!(
            alice_socket.get_peer_name().await.unwrap().expect_err("alice getpeername should fail"),
            fposix::Errno::Enotconn
        );

        // check that alice has no data to read, and it'd block waiting for
        // events:
        assert_eq!(
            alice_socket
                .recv_msg(false, 2048, false, fposix_socket::RecvMsgFlags::empty())
                .await
                .unwrap()
                .expect_err("Reading from alice should fail"),
            fposix::Errno::Eagain
        );
        assert_eq!(
            alice_events
                .wait_handle(ZXSIO_SIGNAL_INCOMING, zx::Time::from_nanos(0))
                .expect_err("Alice incoming event should not be signaled"),
            zx::Status::TIMED_OUT
        );

        // Setup Bob as a client, bound to REMOTE_ADDR:300
        println!("Configuring bob...");
        let bob = t.get(1);
        let (bob_socket, bob_events) = get_socket_and_event::<A>(bob, proto).await;
        let () = bob_socket
            .bind(&mut A::create(A::REMOTE_ADDR, 300))
            .await
            .unwrap()
            .expect("bob bind suceeds");

        // Verify that Bob is listening on the local socket, but has no peer
        // socket
        assert_eq!(
            bob_socket.get_sock_name().await.unwrap().expect("bob getsockname suceeds"),
            A::create(A::REMOTE_ADDR, 300)
        );
        assert_eq!(
            bob_socket
                .get_peer_name()
                .await
                .unwrap()
                .expect_err("get peer name should fail before connected"),
            fposix::Errno::Enotconn
        );

        // Connect Bob to Alice on LOCAL_ADDR:200
        println!("Connecting bob to alice...");
        let () = bob_socket
            .connect(&mut A::create(A::LOCAL_ADDR, 200))
            .await
            .unwrap()
            .expect("Connect succeeds");

        // Verify that Bob has the peer socket set correctly
        assert_eq!(
            bob_socket.get_peer_name().await.unwrap().expect("bob getpeername suceeds"),
            A::create(A::LOCAL_ADDR, 200)
        );

        // We don't care which signals are on, only that SIGNAL_OUTGOING is, we
        // can ignore the return value.
        let _signals = bob_events
            .wait_handle(ZXSIO_SIGNAL_OUTGOING, zx::Time::from_nanos(0))
            .expect("Bob outgoing event should be signaled");

        // Send datagram from Bob's socket.
        println!("Writing datagram to bob");
        let body = "Hello".as_bytes();
        assert_eq!(
            bob_socket
                .send_msg(
                    None,
                    &body,
                    fposix_socket::DatagramSocketSendControlData::EMPTY,
                    fposix_socket::SendMsgFlags::empty()
                )
                .await
                .unwrap()
                .expect("sendmsg suceeds"),
            body.len() as i64
        );

        // Wait for datagram to arrive on Alice's socket:

        println!("Waiting for signals");
        assert_eq!(
            fasync::OnSignals::new(&alice_events, ZXSIO_SIGNAL_INCOMING).await,
            Ok(ZXSIO_SIGNAL_INCOMING | ZXSIO_SIGNAL_OUTGOING)
        );

        let (from, data, _, truncated) = alice_socket
            .recv_msg(true, 2048, false, fposix_socket::RecvMsgFlags::empty())
            .await
            .unwrap()
            .expect("recvmsg suceeeds");
        let source = A::from_sock_addr(*from.expect("socket address returned"))
            .expect("bad socket address return");
        assert_eq!(source.addr(), A::REMOTE_ADDR);
        assert_eq!(source.port(), 300);
        assert_eq!(truncated, 0);
        assert_eq!(&data[..], body);
    }

    declare_tests!(
        hello,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    async fn socket_describe(
        domain: fposix_socket::Domain,
        proto: fposix_socket::DatagramSocketProtocol,
    ) {
        let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
        let test_stack = t.get(0);
        let socket_provider = test_stack.connect_socket_provider().unwrap();
        let response = socket_provider
            .datagram_socket(domain, proto)
            .await
            .unwrap()
            .expect("Socket call succeeds");
        let socket = match response {
            fposix_socket::ProviderDatagramSocketResponse::SynchronousDatagramSocket(sock) => sock,
            // TODO(https://fxrev.dev/99905): Implement Fast UDP sockets in Netstack3.
            fposix_socket::ProviderDatagramSocketResponse::DatagramSocket(sock) => {
                let _: fidl::endpoints::ClientEnd<fposix_socket::DatagramSocketMarker> = sock;
                panic!("expected SynchronousDatagramSocket, found DatagramSocket")
            }
        };
        let fposix_socket::SynchronousDatagramSocketDescribeResponse { event, .. } =
            socket.into_proxy().unwrap().describe().await.expect("Describe call succeeds");
        let _: zx::EventPair = event.expect("Describe call returns event");
    }

    #[fasync::run_singlethreaded(test)]
    async fn udp_v4_socket_describe() {
        socket_describe(fposix_socket::Domain::Ipv4, fposix_socket::DatagramSocketProtocol::Udp)
            .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn udp_v6_socket_describe() {
        socket_describe(fposix_socket::Domain::Ipv6, fposix_socket::DatagramSocketProtocol::Udp)
            .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn icmp_echo_v4_socket_describe() {
        socket_describe(
            fposix_socket::Domain::Ipv4,
            fposix_socket::DatagramSocketProtocol::IcmpEcho,
        )
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn icmp_echo_v6_socket_describe() {
        socket_describe(
            fposix_socket::Domain::Ipv6,
            fposix_socket::DatagramSocketProtocol::IcmpEcho,
        )
        .await
    }

    async fn socket_get_info(
        domain: fposix_socket::Domain,
        proto: fposix_socket::DatagramSocketProtocol,
    ) {
        let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
        let test_stack = t.get(0);
        let socket_provider = test_stack.connect_socket_provider().unwrap();
        let response = socket_provider
            .datagram_socket(domain, proto)
            .await
            .unwrap()
            .expect("Socket call succeeds");
        let socket = match response {
            fposix_socket::ProviderDatagramSocketResponse::SynchronousDatagramSocket(sock) => sock,
            // TODO(https://fxrev.dev/99905): Implement Fast UDP sockets in Netstack3.
            fposix_socket::ProviderDatagramSocketResponse::DatagramSocket(sock) => {
                let _: fidl::endpoints::ClientEnd<fposix_socket::DatagramSocketMarker> = sock;
                panic!("expected SynchronousDatagramSocket, found DatagramSocket")
            }
        };
        let info = socket.into_proxy().unwrap().get_info().await.expect("get_info call succeeds");
        assert_eq!(info, Ok((domain, proto)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn udp_v4_socket_get_info() {
        socket_get_info(fposix_socket::Domain::Ipv4, fposix_socket::DatagramSocketProtocol::Udp)
            .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn udp_v6_socket_get_info() {
        socket_get_info(fposix_socket::Domain::Ipv6, fposix_socket::DatagramSocketProtocol::Udp)
            .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn icmp_echo_v4_socket_get_info() {
        socket_get_info(
            fposix_socket::Domain::Ipv4,
            fposix_socket::DatagramSocketProtocol::IcmpEcho,
        )
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn icmp_echo_v6_socket_get_info() {
        socket_get_info(
            fposix_socket::Domain::Ipv6,
            fposix_socket::DatagramSocketProtocol::IcmpEcho,
        )
        .await
    }

    async fn socket_clone(
        socket: &fposix_socket::SynchronousDatagramSocketProxy,
    ) -> Result<fposix_socket::SynchronousDatagramSocketProxy, Error> {
        let (client, server) =
            fidl::endpoints::create_proxy::<fposix_socket::SynchronousDatagramSocketMarker>()?;
        let server = ServerEnd::new(server.into_channel());
        let () = socket.clone2(server)?;
        Ok(client)
    }

    async fn clone<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol)
    where
        <A::AddrType as IpAddress>::Version: SocketCollectionIpExt<T>,
        T: Transport<Ipv4>,
        T: Transport<Ipv6>,
        T: Transport<<A::AddrType as IpAddress>::Version>,
        crate::bindings::BindingsNonSyncCtxImpl: AsRef<SocketCollectionPair<T>>,
    {
        let mut t = TestSetupBuilder::new()
            .add_endpoint()
            .add_endpoint()
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint(test_ep_name(1), Some(A::config_addr_subnet())),
            )
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint(test_ep_name(2), Some(A::config_addr_subnet_remote())),
            )
            .build()
            .await
            .unwrap();
        let (alice_socket, alice_events) = get_socket_and_event::<A>(t.get(0), proto).await;
        let alice_cloned = socket_clone(&alice_socket).await.expect("cannot clone socket");
        let fposix_socket::SynchronousDatagramSocketDescribeResponse { event: alice_event, .. } =
            alice_cloned.describe().await.expect("Describe call succeeds");
        let _: zx::EventPair = alice_event.expect("Describe call returns event");

        let () = alice_socket
            .bind(&mut A::create(A::LOCAL_ADDR, 200))
            .await
            .unwrap()
            .expect("failed to bind for alice");
        // We should be able to read that back from the cloned socket.
        assert_eq!(
            alice_cloned.get_sock_name().await.unwrap().expect("failed to getsockname for alice"),
            A::create(A::LOCAL_ADDR, 200)
        );

        let (bob_socket, bob_events) = get_socket_and_event::<A>(t.get(1), proto).await;
        let bob_cloned = socket_clone(&bob_socket).await.expect("failed to clone socket");
        let () = bob_cloned
            .bind(&mut A::create(A::REMOTE_ADDR, 200))
            .await
            .unwrap()
            .expect("failed to bind for bob");
        // We should be able to read that back from the original socket.
        assert_eq!(
            bob_socket.get_sock_name().await.unwrap().expect("failed to getsockname for bob"),
            A::create(A::REMOTE_ADDR, 200)
        );

        let body = "Hello".as_bytes();
        assert_eq!(
            alice_socket
                .send_msg(
                    Some(&mut A::create(A::REMOTE_ADDR, 200)),
                    &body,
                    fposix_socket::DatagramSocketSendControlData::EMPTY,
                    fposix_socket::SendMsgFlags::empty()
                )
                .await
                .unwrap()
                .expect("failed to send_msg"),
            body.len() as i64
        );

        assert_eq!(
            fasync::OnSignals::new(&bob_events, ZXSIO_SIGNAL_INCOMING).await,
            Ok(ZXSIO_SIGNAL_INCOMING | ZXSIO_SIGNAL_OUTGOING)
        );

        // Receive from the cloned socket.
        let (from, data, _, truncated) = bob_cloned
            .recv_msg(true, 2048, false, fposix_socket::RecvMsgFlags::empty())
            .await
            .unwrap()
            .expect("failed to recv_msg");
        assert_eq!(&data[..], body);
        assert_eq!(truncated, 0);
        assert_eq!(from.map(|a| *a), Some(A::create(A::LOCAL_ADDR, 200)));
        // The data have already been received on the cloned socket
        assert_eq!(
            bob_socket
                .recv_msg(false, 2048, false, fposix_socket::RecvMsgFlags::empty())
                .await
                .unwrap()
                .expect_err("Reading from bob should fail"),
            fposix::Errno::Eagain
        );

        // Close the socket should not invalidate the cloned socket.
        let () = bob_socket
            .close()
            .await
            .expect("FIDL error")
            .map_err(zx::Status::from_raw)
            .expect("close failed");

        assert_eq!(
            bob_cloned
                .send_msg(
                    Some(&mut A::create(A::LOCAL_ADDR, 200)),
                    &body,
                    fposix_socket::DatagramSocketSendControlData::EMPTY,
                    fposix_socket::SendMsgFlags::empty()
                )
                .await
                .unwrap()
                .expect("failed to send_msg"),
            body.len() as i64
        );

        let () = alice_cloned
            .close()
            .await
            .expect("FIDL error")
            .map_err(zx::Status::from_raw)
            .expect("close failed");
        assert_eq!(
            fasync::OnSignals::new(&alice_events, ZXSIO_SIGNAL_INCOMING).await,
            Ok(ZXSIO_SIGNAL_INCOMING | ZXSIO_SIGNAL_OUTGOING)
        );

        let (from, data, _, truncated) = alice_socket
            .recv_msg(true, 2048, false, fposix_socket::RecvMsgFlags::empty())
            .await
            .unwrap()
            .expect("failed to recv_msg");
        assert_eq!(&data[..], body);
        assert_eq!(truncated, 0);
        assert_eq!(from.map(|a| *a), Some(A::create(A::REMOTE_ADDR, 200)));

        // Make sure the sockets are still in the stack.
        for i in 0..2 {
            t.get(i)
                .with_ctx(|ctx| {
                    let SocketCollection { conns, listeners } =
                        <A::AddrType as IpAddress>::Version::get_collection(&ctx.non_sync_ctx);
                    assert_matches!(conns.iter().collect::<Vec<_>>()[..], []);
                    assert_matches!(listeners.iter().collect::<Vec<_>>()[..], [_]);
                })
                .await;
        }

        let () = alice_socket
            .close()
            .await
            .expect("FIDL error")
            .map_err(zx::Status::from_raw)
            .expect("close failed");
        let () = bob_cloned
            .close()
            .await
            .expect("FIDL error")
            .map_err(zx::Status::from_raw)
            .expect("close failed");

        // But the sockets should have gone here.
        for i in 0..2 {
            t.get(i)
                .with_ctx(|ctx| {
                    let SocketCollection { conns, listeners } =
                        <A::AddrType as IpAddress>::Version::get_collection(&ctx.non_sync_ctx);
                    assert_matches!(conns.iter().collect::<Vec<_>>()[..], []);
                    assert_matches!(listeners.iter().collect::<Vec<_>>()[..], []);
                })
                .await;
        }
    }

    declare_tests!(
        clone,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    async fn close_twice<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol)
    where
        <A::AddrType as IpAddress>::Version: SocketCollectionIpExt<T>,
        T: Transport<Ipv4>,
        T: Transport<Ipv6>,
        T: Transport<<A::AddrType as IpAddress>::Version>,
        crate::bindings::BindingsNonSyncCtxImpl: AsRef<SocketCollectionPair<T>>,
    {
        // Make sure we cannot close twice from the same channel so that we
        // maintain the correct refcount.
        let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
        let test_stack = t.get(0);
        let socket = get_socket::<A>(test_stack, proto).await;
        let cloned = socket_clone(&socket).await.unwrap();
        let () = socket
            .close()
            .await
            .expect("FIDL error")
            .map_err(zx::Status::from_raw)
            .expect("close failed");
        let _: fidl::Error = socket
            .close()
            .await
            .expect_err("should not be able to close the socket twice on the same channel");
        assert!(socket.into_channel().unwrap().is_closed());
        // Since we still hold the cloned socket, the binding_data shouldn't be
        // empty
        test_stack
            .with_ctx(|ctx| {
                let SocketCollection { conns, listeners } =
                    <A::AddrType as IpAddress>::Version::get_collection(&ctx.non_sync_ctx);
                assert_matches!(conns.iter().collect::<Vec<_>>()[..], []);
                assert_matches!(listeners.iter().collect::<Vec<_>>()[..], []);
            })
            .await;
        let () = cloned
            .close()
            .await
            .expect("FIDL error")
            .map_err(zx::Status::from_raw)
            .expect("close failed");
        // Now it should become empty
        test_stack
            .with_ctx(|ctx| {
                let SocketCollection { conns, listeners } =
                    <A::AddrType as IpAddress>::Version::get_collection(&ctx.non_sync_ctx);
                assert_matches!(conns.iter().collect::<Vec<_>>()[..], []);
                assert_matches!(listeners.iter().collect::<Vec<_>>()[..], []);
            })
            .await;
    }

    declare_tests!(close_twice);

    async fn implicit_close<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol)
    where
        <A::AddrType as IpAddress>::Version: SocketCollectionIpExt<T>,
        T: Transport<Ipv4>,
        T: Transport<Ipv6>,
        T: Transport<<A::AddrType as IpAddress>::Version>,
        crate::bindings::BindingsNonSyncCtxImpl: AsRef<SocketCollectionPair<T>>,
    {
        let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
        let test_stack = t.get(0);
        let cloned = {
            let socket = get_socket::<A>(test_stack, proto).await;
            socket_clone(&socket).await.unwrap()
            // socket goes out of scope indicating an implicit close.
        };
        // Using an explicit close here.
        let () = cloned
            .close()
            .await
            .expect("FIDL error")
            .map_err(zx::Status::from_raw)
            .expect("close failed");
        // No socket should be there now.
        test_stack
            .with_ctx(|ctx| {
                let SocketCollection { conns, listeners } =
                    <A::AddrType as IpAddress>::Version::get_collection(&ctx.non_sync_ctx);
                assert_matches!(conns.iter().collect::<Vec<_>>()[..], []);
                assert_matches!(listeners.iter().collect::<Vec<_>>()[..], []);
            })
            .await;
    }

    declare_tests!(implicit_close);

    async fn invalid_clone_args<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol)
    where
        <A::AddrType as IpAddress>::Version: SocketCollectionIpExt<T>,
        T: Transport<Ipv4>,
        T: Transport<Ipv6>,
        T: Transport<<A::AddrType as IpAddress>::Version>,
        crate::bindings::BindingsNonSyncCtxImpl: AsRef<SocketCollectionPair<T>>,
    {
        let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
        let test_stack = t.get(0);
        let socket = get_socket::<A>(test_stack, proto).await;
        let () = socket
            .close()
            .await
            .expect("FIDL error")
            .map_err(zx::Status::from_raw)
            .expect("close failed");

        // make sure we don't leak anything.
        test_stack
            .with_ctx(|ctx| {
                let SocketCollection { conns, listeners } =
                    <A::AddrType as IpAddress>::Version::get_collection(&ctx.non_sync_ctx);
                assert_matches!(conns.iter().collect::<Vec<_>>()[..], []);
                assert_matches!(listeners.iter().collect::<Vec<_>>()[..], []);
            })
            .await;
    }

    declare_tests!(invalid_clone_args);

    async fn shutdown<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol) {
        let mut t = TestSetupBuilder::new()
            .add_endpoint()
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint(test_ep_name(1), Some(A::config_addr_subnet())),
            )
            .build()
            .await
            .unwrap();
        let (socket, events) = get_socket_and_event::<A>(t.get(0), proto).await;
        let mut local = A::create(A::LOCAL_ADDR, 200);
        let mut remote = A::create(A::REMOTE_ADDR, 300);
        assert_eq!(
            socket
                .shutdown(fposix_socket::ShutdownMode::WRITE)
                .await
                .unwrap()
                .expect_err("should not shutdown an unconnected socket"),
            fposix::Errno::Enotconn,
        );
        let () = socket.bind(&mut local).await.unwrap().expect("failed to bind");
        assert_eq!(
            socket
                .shutdown(fposix_socket::ShutdownMode::WRITE)
                .await
                .unwrap()
                .expect_err("should not shutdown an unconnected socket"),
            fposix::Errno::Enotconn,
        );
        let () = socket.connect(&mut remote).await.unwrap().expect("failed to connect");
        assert_eq!(
            socket
                .shutdown(fposix_socket::ShutdownMode::empty())
                .await
                .unwrap()
                .expect_err("invalid args"),
            fposix::Errno::Einval
        );

        // Cannot send
        let body = "Hello".as_bytes();
        let () = socket
            .shutdown(fposix_socket::ShutdownMode::WRITE)
            .await
            .unwrap()
            .expect("failed to shutdown");
        assert_eq!(
            socket
                .send_msg(
                    None,
                    &body,
                    fposix_socket::DatagramSocketSendControlData::EMPTY,
                    fposix_socket::SendMsgFlags::empty()
                )
                .await
                .unwrap()
                .expect_err("writing to an already-shutdown socket should fail"),
            fposix::Errno::Epipe,
        );
        let mut invalid_addr = A::create(A::REMOTE_ADDR, 0);
        assert_eq!(
            socket.send_msg(Some(&mut invalid_addr), &body, fposix_socket::DatagramSocketSendControlData::EMPTY, fposix_socket::SendMsgFlags::empty()).await.unwrap().expect_err(
                "writing to an invalid address (port 0) should fail with EINVAL instead of EPIPE"
            ),
            fposix::Errno::Einval,
        );

        let (e1, e2) = zx::EventPair::create().unwrap();
        fasync::Task::spawn(async move {
            assert_eq!(
                fasync::OnSignals::new(&events, ZXSIO_SIGNAL_INCOMING).await,
                Ok(ZXSIO_SIGNAL_INCOMING | ZXSIO_SIGNAL_OUTGOING)
            );

            assert_eq!(e1.signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_INCOMING), Ok(()));
        })
        .detach();

        let () = socket
            .shutdown(fposix_socket::ShutdownMode::READ)
            .await
            .unwrap()
            .expect("failed to shutdown");
        let (_, data, _, _) = socket
            .recv_msg(false, 2048, false, fposix_socket::RecvMsgFlags::empty())
            .await
            .unwrap()
            .expect("recvmsg should return empty data");
        assert!(data.is_empty());

        assert_eq!(
            fasync::OnSignals::new(&e2, ZXSIO_SIGNAL_INCOMING).await,
            Ok(ZXSIO_SIGNAL_INCOMING | zx::Signals::EVENTPAIR_CLOSED)
        );

        let () = socket
            .shutdown(fposix_socket::ShutdownMode::READ)
            .await
            .unwrap()
            .expect("failed to shutdown the socket twice");
        let () = socket
            .shutdown(fposix_socket::ShutdownMode::WRITE)
            .await
            .unwrap()
            .expect("failed to shutdown the socket twice");
        let () = socket
            .shutdown(fposix_socket::ShutdownMode::READ | fposix_socket::ShutdownMode::WRITE)
            .await
            .unwrap()
            .expect("failed to shutdown the socket twice");
    }

    declare_tests!(
        shutdown,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    async fn set_receive_buffer_after_delivery<A: TestSockAddr, T>(
        proto: fposix_socket::DatagramSocketProtocol,
    ) where
        <A::AddrType as IpAddress>::Version: SocketCollectionIpExt<Udp>,
    {
        let mut t =
            TestSetupBuilder::new().add_stack(StackSetupBuilder::new()).build().await.unwrap();

        let (socket, _events) = get_socket_and_event::<A>(t.get(0), proto).await;
        let mut addr =
            A::create(<<A::AddrType as IpAddress>::Version as Ip>::LOOPBACK_ADDRESS.get(), 200);
        socket.bind(&mut addr).await.unwrap().expect("bind should succeed");

        const SENT_PACKETS: u8 = 10;
        for i in 0..SENT_PACKETS {
            let buf = [i; MIN_OUTSTANDING_APPLICATION_MESSAGES_SIZE];
            let sent = socket
                .send_msg(
                    Some(&mut addr),
                    &buf,
                    fposix_socket::DatagramSocketSendControlData::EMPTY,
                    fposix_socket::SendMsgFlags::empty(),
                )
                .await
                .unwrap()
                .expect("send_msg should succeed");
            assert_eq!(sent, MIN_OUTSTANDING_APPLICATION_MESSAGES_SIZE.try_into().unwrap());
        }

        // Wait for all packets to be delivered before changing the buffer size.
        let stack = t.get(0);
        let has_all_delivered = |messages: &MessageQueue<_, _>| {
            messages.queue.available_messages.len() == SENT_PACKETS.into()
        };
        loop {
            let all_delivered = stack
                .with_ctx(|Ctx { sync_ctx: _, non_sync_ctx }| {
                    let SocketCollection { conns: _, listeners } =
                                <<A::AddrType as IpAddress>::Version as SocketCollectionIpExt<
                                    Udp,
                                >>::get_collection(non_sync_ctx);
                    // Check the lone socket to see if the packets were
                    // received.
                    let messages = listeners.iter().next().unwrap();
                    has_all_delivered(&messages.lock())
                })
                .await;
            if all_delivered {
                break;
            }
            // Give other futures on the same executor a chance to run. In a
            // single-threaded context, without the yield, this future would
            // always be able to re-lock the stack after unlocking, and so no
            // other future would make progress.
            futures_lite::future::yield_now().await;
        }

        // Use a buffer size of 0, which will be substituted with the minimum size.
        let () =
            socket.set_receive_buffer(0).await.unwrap().expect("set buffer size should succeed");

        let rx_count = futures::stream::unfold(socket, |socket| async {
            let result = socket
                .recv_msg(false, u32::MAX, false, fposix_socket::RecvMsgFlags::empty())
                .await
                .unwrap();
            match result {
                Ok((addr, data, control, size)) => {
                    let _: (
                        Option<Box<fnet::SocketAddress>>,
                        fposix_socket::DatagramSocketRecvControlData,
                        u32,
                    ) = (addr, control, size);
                    Some((data, socket))
                }
                Err(fposix::Errno::Eagain) => None,
                Err(e) => panic!("unexpected error: {:?}", e),
            }
        })
        .enumerate()
        .map(|(i, data)| {
            assert_eq!(&data, &[i.try_into().unwrap(); MIN_OUTSTANDING_APPLICATION_MESSAGES_SIZE])
        })
        .count()
        .await;
        assert_eq!(rx_count, SENT_PACKETS.into());
    }

    declare_tests!(
        set_receive_buffer_after_delivery,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    async fn send_recv_loopback_peek<A: TestSockAddr, T>(
        proto: fposix_socket::DatagramSocketProtocol,
    ) {
        let (_t, proxy, _event) = prepare_test::<A>(proto).await;
        let mut addr =
            A::create(<<A::AddrType as IpAddress>::Version as Ip>::LOOPBACK_ADDRESS.get(), 100);

        let () = proxy.bind(&mut addr).await.unwrap().expect("bind succeeds");
        let () = proxy.connect(&mut addr).await.unwrap().expect("connect succeeds");

        const DATA: &[u8] = &[1, 2, 3, 4, 5];
        assert_eq!(
            proxy
                .send_msg(
                    None,
                    DATA,
                    fposix_socket::DatagramSocketSendControlData::EMPTY,
                    fposix_socket::SendMsgFlags::empty()
                )
                .await
                .unwrap()
                .expect("send_msg should succeed"),
            DATA.len().try_into().unwrap()
        );

        // First try receiving the message with PEEK set.
        let (_addr, data, _control, truncated) = loop {
            match proxy
                .recv_msg(false, u32::MAX, false, fposix_socket::RecvMsgFlags::PEEK)
                .await
                .unwrap()
            {
                Ok(peek) => break peek,
                Err(fposix::Errno::Eagain) => {
                    // The sent datagram hasn't been received yet, so check for
                    // it again in a moment.
                    continue;
                }
                Err(e) => panic!("unexpected error: {e:?}"),
            }
        };
        assert_eq!(truncated, 0);
        assert_eq!(data.as_slice(), DATA);

        // Now that the message has for sure been received, it can be retrieved
        // without checking for Eagain.
        let (_addr, data, _control, truncated) = proxy
            .recv_msg(false, u32::MAX, false, fposix_socket::RecvMsgFlags::empty())
            .await
            .unwrap()
            .expect("recv should succeed");
        assert_eq!(truncated, 0);
        assert_eq!(data.as_slice(), DATA);
    }

    declare_tests!(
        send_recv_loopback_peek,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    // TODO(https://fxbug.dev/92678): add a syscall test to exercise this
    // behavior.
    async fn multicast_join_receive<A: TestSockAddr, T>(
        proto: fposix_socket::DatagramSocketProtocol,
    ) {
        let (mut t, proxy, event) = prepare_test::<A>(proto).await;

        let mcast_addr = <<A::AddrType as IpAddress>::Version as Ip>::MULTICAST_SUBNET.network();
        let id = t.get(0).get_endpoint_id(1);

        match mcast_addr.into() {
            IpAddr::V4(mcast_addr) => {
                proxy.add_ip_membership(&mut fposix_socket::IpMulticastMembership {
                    mcast_addr: mcast_addr.into_fidl(),
                    iface: id,
                    ..Decodable::new_empty()
                })
            }
            IpAddr::V6(mcast_addr) => {
                proxy.add_ipv6_membership(&mut fposix_socket::Ipv6MulticastMembership {
                    mcast_addr: mcast_addr.into_fidl(),
                    iface: id,
                    ..Decodable::new_empty()
                })
            }
        }
        .await
        .unwrap()
        .expect("add membership should succeed");

        const PORT: u16 = 100;
        const DATA: &[u8] = &[1, 2, 3, 4, 5];

        let () = proxy
            .bind(&mut A::create(
                <<A::AddrType as IpAddress>::Version as Ip>::UNSPECIFIED_ADDRESS,
                PORT,
            ))
            .await
            .unwrap()
            .expect("bind succeeds");

        assert_eq!(
            proxy
                .send_msg(
                    Some(&mut A::create(mcast_addr, PORT)),
                    DATA,
                    fposix_socket::DatagramSocketSendControlData::EMPTY,
                    fposix_socket::SendMsgFlags::empty()
                )
                .await
                .unwrap()
                .expect("send_msg should succeed"),
            DATA.len().try_into().unwrap()
        );

        let _signals = event
            .wait_handle(ZXSIO_SIGNAL_INCOMING, zx::Time::INFINITE)
            .expect("socket should receive");

        let (_addr, data, _control, truncated) = proxy
            .recv_msg(false, u32::MAX, false, fposix_socket::RecvMsgFlags::empty())
            .await
            .unwrap()
            .expect("recv should succeed");
        assert_eq!(truncated, 0);
        assert_eq!(data.as_slice(), DATA);
    }

    declare_tests!(
        multicast_join_receive,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    async fn set_get_hop_limit_unicast<A: TestSockAddr, T>(
        proto: fposix_socket::DatagramSocketProtocol,
    ) {
        let (_t, proxy, _event) = prepare_test::<A>(proto).await;

        const HOP_LIMIT: u8 = 200;
        match <<A::AddrType as IpAddress>::Version as Ip>::VERSION {
            IpVersion::V4 => proxy.set_ip_multicast_ttl(&mut Some(HOP_LIMIT).into_fidl()),
            IpVersion::V6 => proxy.set_ipv6_multicast_hops(&mut Some(HOP_LIMIT).into_fidl()),
        }
        .await
        .unwrap()
        .expect("set hop limit should succeed");

        assert_eq!(
            match <<A::AddrType as IpAddress>::Version as Ip>::VERSION {
                IpVersion::V4 => proxy.get_ip_multicast_ttl(),
                IpVersion::V6 => proxy.get_ipv6_multicast_hops(),
            }
            .await
            .unwrap()
            .expect("get hop limit should succeed"),
            HOP_LIMIT
        )
    }

    declare_tests!(
        set_get_hop_limit_unicast,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    async fn set_get_hop_limit_multicast<A: TestSockAddr, T>(
        proto: fposix_socket::DatagramSocketProtocol,
    ) {
        let (_t, proxy, _event) = prepare_test::<A>(proto).await;

        const HOP_LIMIT: u8 = 200;
        match <<A::AddrType as IpAddress>::Version as Ip>::VERSION {
            IpVersion::V4 => proxy.set_ip_ttl(&mut Some(HOP_LIMIT).into_fidl()),
            IpVersion::V6 => proxy.set_ipv6_unicast_hops(&mut Some(HOP_LIMIT).into_fidl()),
        }
        .await
        .unwrap()
        .expect("set hop limit should succeed");

        assert_eq!(
            match <<A::AddrType as IpAddress>::Version as Ip>::VERSION {
                IpVersion::V4 => proxy.get_ip_ttl(),
                IpVersion::V6 => proxy.get_ipv6_unicast_hops(),
            }
            .await
            .unwrap()
            .expect("get hop limit should succeed"),
            HOP_LIMIT
        )
    }

    declare_tests!(
        set_get_hop_limit_multicast,
        icmp #[should_panic = "not yet implemented: https://fxbug.dev/47321: needs Core implementation"]
    );

    // TODO(https://fxbug.dev/21198): Change this when dual-stack socket support
    // is added since dual-stack sockets should allow setting options for both
    // IP versions.
    async fn set_hop_limit_wrong_type<A: TestSockAddr, T>(
        proto: fposix_socket::DatagramSocketProtocol,
    ) {
        let (_t, proxy, _event) = prepare_test::<A>(proto).await;

        const HOP_LIMIT: u8 = 200;
        assert_matches!(
            match <<A::AddrType as IpAddress>::Version as Ip>::VERSION {
                IpVersion::V4 => proxy.set_ipv6_multicast_hops(&mut Some(HOP_LIMIT).into_fidl()),
                IpVersion::V6 => proxy.set_ip_multicast_ttl(&mut Some(HOP_LIMIT).into_fidl()),
            }
            .await
            .unwrap(),
            Err(_)
        );

        assert_matches!(
            match <<A::AddrType as IpAddress>::Version as Ip>::VERSION {
                IpVersion::V4 => proxy.set_ipv6_unicast_hops(&mut Some(HOP_LIMIT).into_fidl()),
                IpVersion::V6 => proxy.set_ip_ttl(&mut Some(HOP_LIMIT).into_fidl()),
            }
            .await
            .unwrap(),
            Err(_)
        );
    }

    declare_tests!(set_hop_limit_wrong_type);

    // TODO(https://fxbug.dev/21198): Change this when dual-stack socket support
    // is added since dual-stack sockets should allow setting options for both
    // IP versions.
    async fn get_hop_limit_wrong_type<A: TestSockAddr, T>(
        proto: fposix_socket::DatagramSocketProtocol,
    ) {
        let (_t, proxy, _event) = prepare_test::<A>(proto).await;

        assert_matches!(
            match <<A::AddrType as IpAddress>::Version as Ip>::VERSION {
                IpVersion::V4 => proxy.get_ipv6_unicast_hops(),
                IpVersion::V6 => proxy.get_ip_ttl(),
            }
            .await
            .unwrap(),
            Err(_)
        );

        assert_matches!(
            match <<A::AddrType as IpAddress>::Version as Ip>::VERSION {
                IpVersion::V4 => proxy.get_ipv6_multicast_hops(),
                IpVersion::V6 => proxy.get_ip_multicast_ttl(),
            }
            .await
            .unwrap(),
            Err(_)
        );
    }

    declare_tests!(get_hop_limit_wrong_type);
}
