// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Datagram socket bindings.

use std::convert::TryInto as _;
use std::marker::PhantomData;
use std::num::NonZeroU16;
use std::ops::{Deref as _, DerefMut as _};

use fidl_fuchsia_io as fio;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_posix as fposix;
use fidl_fuchsia_posix_socket as fposix_socket;

use anyhow::{format_err, Error};
use fidl::endpoints::{RequestStream as _, ServerEnd};
use fidl::AsyncChannel;
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, prelude::HandleBased as _, Peered as _};
use futures::{StreamExt as _, TryFutureExt as _};
use log::{error, trace};
use net_types::{
    ip::{Ip, Ipv4, Ipv6},
    SpecifiedAddr,
};
use netstack3_core::{
    connect_udp, get_udp_conn_info, get_udp_listener_info, icmp, listen_udp, remove_udp_conn,
    remove_udp_listener, send_udp, send_udp_conn, send_udp_listener, BufferDispatcher,
    BufferUdpContext, BufferUdpStateContext, Ctx, EventDispatcher, IdMap, IdMapCollection,
    IdMapCollectionKey, IpExt, IpSockCreationError, IpSockSendError, LocalAddressError,
    TransportIpContext, UdpConnId, UdpConnInfo, UdpContext, UdpListenerId, UdpListenerInfo,
    UdpSendError, UdpSendListenerError, UdpSockCreationError, UdpStateContext,
};
use packet::{Buf, BufferMut, SerializeError};
use packet_formats::{
    error::ParseError,
    icmp::{
        IcmpEchoReply, IcmpEchoRequest, IcmpIpExt, IcmpMessage, IcmpPacket, IcmpPacketBuilder,
        IcmpParseArgs, IcmpUnusedCode,
    },
};
use std::collections::VecDeque;
use thiserror::Error;

use crate::bindings::{Lockable, LockableContext};

use super::{
    IntoErrno, IpSockAddrExt, SockAddr, SocketWorkerProperties, ZXSIO_SIGNAL_INCOMING,
    ZXSIO_SIGNAL_OUTGOING,
};

/// Limits the number of messages that can be queued for an application to be
/// read before we start dropping packets.
// TODO(brunodalbo) move this to a buffer pool instead.
const MAX_OUTSTANDING_APPLICATION_MESSAGES: usize = 50;

/// A minimal abstraction over transport protocols that allows bindings-side state to be stored.
pub(crate) trait Transport<I>: std::fmt::Debug {
    type ConnId: std::fmt::Debug + Copy + IdMapCollectionKey;
    type ListenerId: std::fmt::Debug + Copy + IdMapCollectionKey;
}

pub(crate) struct SocketCollection<I: Ip, T: Transport<I>> {
    binding_data: IdMap<BindingData<I, T>>,
    conns: IdMapCollection<T::ConnId, usize>,
    listeners: IdMapCollection<T::ListenerId, usize>,
}

impl<I: Ip, T: Transport<I>> Default for SocketCollection<I, T> {
    fn default() -> Self {
        Self {
            binding_data: Default::default(),
            conns: Default::default(),
            listeners: Default::default(),
        }
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
pub(crate) trait TransportState<I: Ip, C>: Transport<I> {
    type CreateConnError: IntoErrno;
    type CreateListenerError: IntoErrno;
    type LocalIdentifier: OptionFromU16 + Into<u16>;
    type RemoteIdentifier: OptionFromU16 + Into<u16>;

    fn create_connection(
        ctx: &mut C,
        local_ip: Option<SpecifiedAddr<I::Addr>>,
        local_id: Option<Self::LocalIdentifier>,
        remote_ip: SpecifiedAddr<I::Addr>,
        remote_id: Self::RemoteIdentifier,
    ) -> Result<Self::ConnId, Self::CreateConnError>;

    fn create_listener(
        ctx: &mut C,
        addr: Option<SpecifiedAddr<I::Addr>>,
        port: Option<Self::LocalIdentifier>,
    ) -> Result<Self::ListenerId, Self::CreateListenerError>;

    fn get_conn_info(
        ctx: &C,
        id: Self::ConnId,
    ) -> (
        SpecifiedAddr<I::Addr>,
        Self::LocalIdentifier,
        SpecifiedAddr<I::Addr>,
        Self::RemoteIdentifier,
    );

    fn get_listener_info(
        ctx: &C,
        id: Self::ListenerId,
    ) -> (Option<SpecifiedAddr<I::Addr>>, Self::LocalIdentifier);

    fn remove_conn(
        ctx: &mut C,
        id: Self::ConnId,
    ) -> (
        SpecifiedAddr<I::Addr>,
        Self::LocalIdentifier,
        SpecifiedAddr<I::Addr>,
        Self::RemoteIdentifier,
    );

    fn remove_listener(
        ctx: &mut C,
        id: Self::ListenerId,
    ) -> (Option<SpecifiedAddr<I::Addr>>, Self::LocalIdentifier);
}

/// An abstraction over transport protocols that allows data to be sent via the Core.
pub(crate) trait BufferTransportState<I: Ip, B: BufferMut, C>: TransportState<I, C> {
    type SendError: IntoErrno;
    type SendConnError: IntoErrno;
    type SendListenerError: IntoErrno;

    fn send(
        ctx: &mut C,
        local_ip: Option<SpecifiedAddr<I::Addr>>,
        local_id: Option<Self::LocalIdentifier>,
        remote_ip: SpecifiedAddr<I::Addr>,
        remote_id: Self::RemoteIdentifier,
        body: B,
    ) -> Result<(), (B, Self::SendError)>;

    fn send_conn(ctx: &mut C, conn: Self::ConnId, body: B) -> Result<(), (B, Self::SendConnError)>;

    fn send_listener(
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
    type ConnId = UdpConnId<I>;
    type ListenerId = UdpListenerId<I>;
}

impl OptionFromU16 for NonZeroU16 {
    fn from_u16(t: u16) -> Option<Self> {
        Self::new(t)
    }
}

impl<I: IpExt, C: UdpStateContext<I>> TransportState<I, C> for Udp {
    type CreateConnError = UdpSockCreationError;
    type CreateListenerError = LocalAddressError;
    type LocalIdentifier = NonZeroU16;
    type RemoteIdentifier = NonZeroU16;

    fn create_connection(
        ctx: &mut C,
        local_ip: Option<SpecifiedAddr<I::Addr>>,
        local_id: Option<Self::LocalIdentifier>,
        remote_ip: SpecifiedAddr<I::Addr>,
        remote_id: Self::RemoteIdentifier,
    ) -> Result<Self::ConnId, Self::CreateConnError> {
        connect_udp(ctx, local_ip, local_id, remote_ip, remote_id)
    }

    fn create_listener(
        ctx: &mut C,
        addr: Option<SpecifiedAddr<I::Addr>>,
        port: Option<Self::LocalIdentifier>,
    ) -> Result<Self::ListenerId, Self::CreateListenerError> {
        listen_udp(ctx, addr, port)
    }

    fn get_conn_info(
        ctx: &C,
        id: Self::ConnId,
    ) -> (
        SpecifiedAddr<I::Addr>,
        Self::LocalIdentifier,
        SpecifiedAddr<I::Addr>,
        Self::RemoteIdentifier,
    ) {
        let UdpConnInfo { local_ip, local_port, remote_ip, remote_port } =
            get_udp_conn_info(ctx, id);
        (local_ip, local_port, remote_ip, remote_port)
    }

    fn get_listener_info(
        ctx: &C,
        id: Self::ListenerId,
    ) -> (Option<SpecifiedAddr<I::Addr>>, Self::LocalIdentifier) {
        let UdpListenerInfo { local_ip, local_port } = get_udp_listener_info(ctx, id);
        (local_ip, local_port)
    }

    fn remove_conn(
        ctx: &mut C,
        id: Self::ConnId,
    ) -> (
        SpecifiedAddr<I::Addr>,
        Self::LocalIdentifier,
        SpecifiedAddr<I::Addr>,
        Self::RemoteIdentifier,
    ) {
        let UdpConnInfo { local_ip, local_port, remote_ip, remote_port } = remove_udp_conn(ctx, id);
        (local_ip, local_port, remote_ip, remote_port)
    }

    fn remove_listener(
        ctx: &mut C,
        id: Self::ListenerId,
    ) -> (Option<SpecifiedAddr<I::Addr>>, Self::LocalIdentifier) {
        let UdpListenerInfo { local_ip, local_port } = remove_udp_listener(ctx, id);
        (local_ip, local_port)
    }
}

impl<I: IpExt, B: BufferMut, C: BufferUdpStateContext<I, B>> BufferTransportState<I, B, C> for Udp {
    type SendError = UdpSendError;
    type SendConnError = IpSockSendError;
    type SendListenerError = UdpSendListenerError;

    fn send(
        ctx: &mut C,
        local_ip: Option<SpecifiedAddr<I::Addr>>,
        local_id: Option<Self::LocalIdentifier>,
        remote_ip: SpecifiedAddr<I::Addr>,
        remote_id: Self::RemoteIdentifier,
        body: B,
    ) -> Result<(), (B, Self::SendError)> {
        send_udp(ctx, local_ip, local_id, remote_ip, remote_id, body)
    }

    fn send_conn(ctx: &mut C, conn: Self::ConnId, body: B) -> Result<(), (B, Self::SendConnError)> {
        send_udp_conn(ctx, conn, body)
    }

    fn send_listener(
        ctx: &mut C,
        listener: Self::ListenerId,
        local_ip: Option<SpecifiedAddr<I::Addr>>,
        remote_ip: SpecifiedAddr<I::Addr>,
        remote_id: Self::RemoteIdentifier,
        body: B,
    ) -> Result<(), (B, Self::SendListenerError)> {
        send_udp_listener(ctx, listener, local_ip, remote_ip, remote_id, body)
    }
}

impl<I: icmp::IcmpIpExt> UdpContext<I> for SocketCollection<I, Udp> {
    fn receive_icmp_error(
        &mut self,
        id: Result<UdpConnId<I>, UdpListenerId<I>>,
        err: I::ErrorCode,
    ) {
        let Self { binding_data, conns, listeners } = self;
        let id = match id.as_ref() {
            Ok(conn) => conns.get(conn),
            Err(listener) => listeners.get(listener),
        };
        let binding_data = id.copied().and_then(|id| binding_data.get(id));
        // NB: Logging at error as a means of failing tests that provoke this condition.
        error!("unimplemented receive_icmp_error {:?} on {:?}", err, binding_data)
    }
}

impl<I: IpExt, B: BufferMut> BufferUdpContext<I, B> for SocketCollection<I, Udp> {
    fn receive_udp_from_conn(
        &mut self,
        conn: UdpConnId<I>,
        src_ip: I::Addr,
        src_port: NonZeroU16,
        body: B,
    ) {
        let Self { binding_data, conns, listeners: _ } = self;
        let binding_data =
            conns.get(&conn).copied().and_then(|id| binding_data.get_mut(id)).unwrap();
        match binding_data.receive_datagram(src_ip, src_port.get(), body.as_ref()) {
            Ok(()) => (),
            Err(e) => error!("receive_udp_from_conn failed: {:?}", e),
        }
    }

    fn receive_udp_from_listen(
        &mut self,
        listener: UdpListenerId<I>,
        src_ip: I::Addr,
        _dst_ip: I::Addr,
        src_port: Option<NonZeroU16>,
        body: B,
    ) {
        let Self { binding_data, conns: _, listeners } = self;
        let binding_data =
            listeners.get(&listener).copied().and_then(|id| binding_data.get_mut(id)).unwrap();
        match binding_data.receive_datagram(
            src_ip,
            src_port.map_or(0, NonZeroU16::get),
            body.as_ref(),
        ) {
            Ok(()) => (),
            Err(e) => error!("receive_udp_from_conn failed: {:?}", e),
        }
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
    fn new_icmp_connection<D: EventDispatcher>(
        ctx: &mut Ctx<D>,
        local_addr: Option<SpecifiedAddr<Self::Addr>>,
        local_id: Option<<IcmpEcho as TransportState<Self, Ctx<D>>>::LocalIdentifier>,
        remote_addr: SpecifiedAddr<Self::Addr>,
    ) -> Result<icmp::IcmpConnId<Self>, icmp::IcmpSockCreationError>;

    fn send_icmp_echo_request<B: BufferMut, D: BufferDispatcher<B>>(
        ctx: &mut Ctx<D>,
        conn: icmp::IcmpConnId<Self>,
        seq: u16,
        body: B,
    ) -> Result<(), (B, IcmpSendError)>;

    fn send_conn<B: BufferMut, D: BufferDispatcher<B>>(
        ctx: &mut Ctx<D>,
        conn: icmp::IcmpConnId<Self>,
        mut body: B,
    ) -> Result<(), (B, IcmpSendError)>
    where
        IcmpEchoRequest: for<'a> IcmpMessage<Self, &'a [u8]>,
    {
        use net_types::Witness as _;

        let (src_ip, _id, dst_ip, IcmpRemoteIdentifier {}) = IcmpEcho::get_conn_info(ctx, conn);
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
        Self::send_icmp_echo_request(ctx, conn, seq, body)
    }
}

impl IcmpEchoIpExt for Ipv4 {
    fn new_icmp_connection<D: EventDispatcher>(
        ctx: &mut Ctx<D>,
        local_addr: Option<SpecifiedAddr<Self::Addr>>,
        local_id: Option<<IcmpEcho as TransportState<Self, Ctx<D>>>::LocalIdentifier>,
        remote_addr: SpecifiedAddr<Self::Addr>,
    ) -> Result<icmp::IcmpConnId<Self>, icmp::IcmpSockCreationError> {
        icmp::new_icmpv4_connection(ctx, local_addr, remote_addr, local_id.unwrap_or_default())
    }

    fn send_icmp_echo_request<B: BufferMut, D: BufferDispatcher<B>>(
        ctx: &mut Ctx<D>,
        conn: icmp::IcmpConnId<Self>,
        seq: u16,
        body: B,
    ) -> Result<(), (B, IcmpSendError)> {
        icmp::send_icmpv4_echo_request(ctx, conn, seq, body)
            .map_err(|(body, err)| (body, err.into()))
    }
}

impl IcmpEchoIpExt for Ipv6 {
    fn new_icmp_connection<D: EventDispatcher>(
        ctx: &mut Ctx<D>,
        local_addr: Option<SpecifiedAddr<Self::Addr>>,
        local_id: Option<<IcmpEcho as TransportState<Self, Ctx<D>>>::LocalIdentifier>,
        remote_addr: SpecifiedAddr<Self::Addr>,
    ) -> Result<icmp::IcmpConnId<Self>, icmp::IcmpSockCreationError> {
        icmp::new_icmpv6_connection(ctx, local_addr, remote_addr, local_id.unwrap_or_default())
    }

    fn send_icmp_echo_request<B: BufferMut, D: BufferDispatcher<B>>(
        ctx: &mut Ctx<D>,
        conn: icmp::IcmpConnId<Self>,
        seq: u16,
        body: B,
    ) -> Result<(), (B, IcmpSendError)> {
        icmp::send_icmpv6_echo_request(ctx, conn, seq, body)
            .map_err(|(body, err)| (body, err.into()))
    }
}

impl OptionFromU16 for u16 {
    fn from_u16(t: u16) -> Option<Self> {
        Some(t)
    }
}

impl<I: IcmpEchoIpExt, D: EventDispatcher> TransportState<I, Ctx<D>> for IcmpEcho {
    type CreateConnError = icmp::IcmpSockCreationError;
    type CreateListenerError = icmp::IcmpSockCreationError;
    type LocalIdentifier = u16;
    type RemoteIdentifier = IcmpRemoteIdentifier;

    fn create_connection(
        ctx: &mut Ctx<D>,
        local_addr: Option<SpecifiedAddr<I::Addr>>,
        local_id: Option<Self::LocalIdentifier>,
        remote_addr: SpecifiedAddr<I::Addr>,
        remote_id: Self::RemoteIdentifier,
    ) -> Result<Self::ConnId, Self::CreateConnError> {
        let IcmpRemoteIdentifier {} = remote_id;
        I::new_icmp_connection(ctx, local_addr, local_id, remote_addr)
    }

    fn create_listener(
        _ctx: &mut Ctx<D>,
        _addr: Option<SpecifiedAddr<I::Addr>>,
        _id: Option<Self::LocalIdentifier>,
    ) -> Result<Self::ListenerId, Self::CreateListenerError> {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn get_conn_info(
        _ctx: &Ctx<D>,
        _id: Self::ConnId,
    ) -> (
        SpecifiedAddr<I::Addr>,
        Self::LocalIdentifier,
        SpecifiedAddr<I::Addr>,
        Self::RemoteIdentifier,
    ) {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn get_listener_info(
        _ctx: &Ctx<D>,
        _id: Self::ListenerId,
    ) -> (Option<SpecifiedAddr<I::Addr>>, Self::LocalIdentifier) {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn remove_conn(
        _ctx: &mut Ctx<D>,
        _id: Self::ConnId,
    ) -> (
        SpecifiedAddr<I::Addr>,
        Self::LocalIdentifier,
        SpecifiedAddr<I::Addr>,
        Self::RemoteIdentifier,
    ) {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn remove_listener(
        _ctx: &mut Ctx<D>,
        _id: Self::ListenerId,
    ) -> (Option<SpecifiedAddr<I::Addr>>, Self::LocalIdentifier) {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }
}

impl<I: IcmpEchoIpExt, B: BufferMut, D: BufferDispatcher<B>> BufferTransportState<I, B, Ctx<D>>
    for IcmpEcho
where
    IcmpEchoRequest: for<'a> IcmpMessage<I, &'a [u8]>,
{
    type SendError = IcmpSendError;
    type SendConnError = IcmpSendError;
    type SendListenerError = IcmpSendError;

    fn send(
        _ctx: &mut Ctx<D>,
        _local_ip: Option<SpecifiedAddr<I::Addr>>,
        _local_id: Option<Self::LocalIdentifier>,
        _remote_ip: SpecifiedAddr<I::Addr>,
        _remote_id: Self::RemoteIdentifier,
        _body: B,
    ) -> Result<(), (B, Self::SendError)> {
        todo!("https://fxbug.dev/47321: needs Core implementation")
    }

    fn send_conn(
        ctx: &mut Ctx<D>,
        conn: Self::ConnId,
        body: B,
    ) -> Result<(), (B, Self::SendConnError)> {
        I::send_conn(ctx, conn, body)
    }

    fn send_listener(
        _ctx: &mut Ctx<D>,
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
        let Self { binding_data, conns, listeners: _ } = self;
        let binding_data =
            conns.get(&conn).copied().and_then(|id| binding_data.get_mut(id)).unwrap();
        // NB: Logging at error as a means of failing tests that provoke this condition.
        error!("unimplemented receive_icmp_error {:?} seq={} on {:?}", err, seq_num, binding_data)
    }

    fn close_icmp_connection(&mut self, conn: icmp::IcmpConnId<I>, err: IpSockCreationError) {
        let Self { binding_data, conns, listeners: _ } = self;
        let binding_data =
            conns.get(&conn).copied().and_then(|id| binding_data.get_mut(id)).unwrap();
        todo!("https://fxbug.dev/47321: err={}; ICMP should 'stay open' on {:?}", err, binding_data)
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
                let Self { binding_data, conns, listeners: _ } = self;
                let binding_data =
                    conns.get(&conn).copied().and_then(|id| binding_data.get_mut(id)).unwrap();
                match binding_data.receive_datagram(src_ip, id, body.as_ref()) {
                    Ok(()) => (),
                    Err(e) => error!("receive_udp_from_conn failed: {:?}", e),
                }
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

#[derive(Debug)]
struct AvailableMessage<A> {
    source_addr: A,
    source_port: u16,
    data: Vec<u8>,
}

#[derive(Debug)]
struct BindingData<I: Ip, T: Transport<I>> {
    local_event: zx::EventPair,
    peer_event: zx::EventPair,
    info: SocketControlInfo<I, T>,
    available_data: VecDeque<AvailableMessage<I::Addr>>,
    ref_count: usize,
}

impl<I: Ip, T: Transport<I>> BindingData<I, T> {
    /// Creates a new `BindingData` with the provided event pair and
    /// `properties`.
    fn new(
        local_event: zx::EventPair,
        peer_event: zx::EventPair,
        properties: SocketWorkerProperties,
    ) -> Self {
        Self {
            local_event,
            peer_event,
            info: SocketControlInfo { _properties: properties, state: SocketState::Unbound },
            available_data: VecDeque::new(),
            ref_count: 1,
        }
    }

    fn receive_datagram(&mut self, addr: I::Addr, port: u16, body: &[u8]) -> Result<(), Error> {
        if self.available_data.len() >= MAX_OUTSTANDING_APPLICATION_MESSAGES {
            return Err(format_err!("application buffers are full"));
        }

        self.available_data.push_back(AvailableMessage {
            source_addr: addr,
            source_port: port,
            data: body.to_owned(),
        });

        self.local_event.signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_INCOMING)?;

        Ok(())
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
    Unbound,
    BoundListen { listener_id: T::ListenerId },
    BoundConnect { conn_id: T::ConnId, shutdown_read: bool, shutdown_write: bool },
}

impl<I: Ip, T: Transport<I>> SocketState<I, T> {
    fn is_bound(&self) -> bool {
        match self {
            SocketState::Unbound => false,
            SocketState::BoundListen { .. } | SocketState::BoundConnect { .. } => true,
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
    events: fposix_socket::DatagramSocketRequestStream,
    properties: SocketWorkerProperties,
) -> Result<(), fposix::Errno>
where
    C: LockableContext,
    C::Dispatcher: SocketWorkerDispatcher,
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

struct SocketWorker<I, T, C> {
    ctx: C,
    id: usize,
    rights: u32,
    _marker: PhantomData<(I, T)>,
}

impl<I, T, C> SocketWorker<I, T, C>
where
    C: LockableContext,
{
    async fn make_handler(&self) -> RequestHandler<'_, I, T, C> {
        let ctx = self.ctx.lock().await;
        RequestHandler { ctx, binding_id: self.id, rights: self.rights, _marker: PhantomData }
    }
}

impl<I, T, C> SocketWorker<I, T, C>
where
    I: SocketCollectionIpExt<T> + IpExt + IpSockAddrExt,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
    T: TransportState<I, Ctx<<C as RequestHandlerContext<I, T>>::Dispatcher>>,
    T: BufferTransportState<I, Buf<Vec<u8>>, Ctx<<C as RequestHandlerContext<I, T>>::Dispatcher>>,
    C: RequestHandlerContext<I, T>,
    T: Send + Sync + 'static,
    C: Clone + Send + Sync + 'static,
    Ctx<<C as RequestHandlerContext<I, T>>::Dispatcher>: TransportIpContext<I>,
{
    /// Starts servicing events from the provided event stream.
    fn spawn(
        ctx: C,
        properties: SocketWorkerProperties,
        events: fposix_socket::DatagramSocketRequestStream,
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
                let id = {
                    let mut guard = ctx.lock().await;
                    let Ctx { state: _, dispatcher } = &mut *guard;
                    let SocketCollection { binding_data, conns: _, listeners: _ } =
                        I::get_collection_mut(dispatcher);
                    binding_data.push(BindingData::new(local_event, peer_event, properties))
                };
                let worker = Self {
                    ctx,
                    id,
                    rights: fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                    _marker: PhantomData,
                };

                worker.handle_stream(events).await
            }
            // When the closure above finishes, that means `self` goes out of
            // scope and is dropped, meaning that the event stream's underlying
            // channel is closed. If any errors occured as a result of the
            // closure, we just log them.
            .unwrap_or_else(|e: fidl::Error| error!("socket control request error: {:?}", e)),
        )
        .detach();
        Ok(())
    }

    async fn clone(&self) -> Self {
        let mut handler = self.make_handler().await;
        let state = handler.get_state_mut();
        state.ref_count += 1;
        Self { ctx: self.ctx.clone(), id: self.id, rights: self.rights, _marker: PhantomData }
    }

    // Starts servicing a [Clone request](fposix_socket::DatagramSocketRequest::Clone).
    fn clone_spawn(&self, flags: u32, object: ServerEnd<fio::NodeMarker>, mut worker: Self) {
        fasync::Task::spawn(
            async move {
                let channel = AsyncChannel::from_channel(object.into_channel())
                    .expect("failed to create async channel");
                let events = fposix_socket::DatagramSocketRequestStream::from_channel(channel);
                let control_handle = events.control_handle();
                let send_on_open = |status: i32, info: Option<&mut fio::NodeInfo>| {
                    if let Err(e) = control_handle.send_on_open_(status, info) {
                        error!("failed to send OnOpen event with status ({}): {}", status, e);
                    }
                };
                // Datagram sockets don't understand the following flags.
                let append_no_remote =
                    flags & fio::OPEN_FLAG_APPEND != 0 || flags & fio::OPEN_FLAG_NO_REMOTE != 0;
                // Datagram sockets are neither mountable nor executable.
                let executable = flags & fio::OPEN_RIGHT_EXECUTABLE != 0;
                // Cannot specify CLONE_FLAGS_SAME_RIGHTS together with
                // OPEN_RIGHT_* flags.
                let conflicting_rights = flags & fio::CLONE_FLAG_SAME_RIGHTS != 0
                    && (flags & fio::OPEN_RIGHT_READABLE != 0
                        || flags & fio::OPEN_RIGHT_WRITABLE != 0);
                // If CLONE_FLAG_SAME_RIGHTS is not set, then use the
                // intersection of the inherited rights and the newly specified
                // rights.
                let new_rights = flags & (fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE);
                let more_rights_than_original = new_rights & (!worker.rights) > 0;
                if flags & fio::CLONE_FLAG_SAME_RIGHTS == 0 && !more_rights_than_original {
                    worker.rights &= new_rights;
                }

                if append_no_remote || executable || conflicting_rights || more_rights_than_original
                {
                    send_on_open(zx::sys::ZX_ERR_INVALID_ARGS, None);
                    let () = worker.make_handler().await.close();
                    return Ok(());
                }

                if flags & fio::OPEN_FLAG_DESCRIBE != 0 {
                    let mut info = worker.make_handler().await.describe();
                    send_on_open(zx::sys::ZX_OK, info.as_mut());
                }
                worker.handle_stream(events).await
            }
            .unwrap_or_else(|e: fidl::Error| error!("socket control request error: {:?}", e)),
        )
        .detach();
    }

    /// Handles [a stream of POSIX socket requests].
    ///
    /// Returns when getting the first `Close` request.
    ///
    /// [a stream of POSIX socket requests]: fposix_socket::DatagramSocketRequestStream
    async fn handle_stream(
        self,
        mut events: fposix_socket::DatagramSocketRequestStream,
    ) -> Result<(), fidl::Error> {
        // We need to early return here to avoid `Close` requests being received
        // on the same channel twice causing the incorrect decrease of refcount
        // as now the bindings data are potentially shared by several distinct
        // control channels.
        while let Some(event) = events.next().await {
            match event {
                Ok(req) => {
                    match req {
                        fposix_socket::DatagramSocketRequest::Describe { responder } => {
                            // If the call to duplicate_handle fails, we have no
                            // choice but to drop the responder and close the
                            // channel, since Describe must be infallible.
                            if let Some(mut info) = self.make_handler().await.describe() {
                                responder_send!(responder, &mut info);
                            }
                        }
                        fposix_socket::DatagramSocketRequest::Describe2 { query, responder } => {
                            let _ = responder;
                            todo!("https://fxbug.dev/77623: query={:?}", query);
                        }
                        fposix_socket::DatagramSocketRequest::Connect { addr, responder } => {
                            responder_send!(
                                responder,
                                &mut self.make_handler().await.connect(addr)
                            );
                        }
                        fposix_socket::DatagramSocketRequest::Disconnect { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eafnosupport));
                        }
                        fposix_socket::DatagramSocketRequest::Clone { flags, object, .. } => {
                            let cloned_worker = self.clone().await;
                            self.clone_spawn(flags, object, cloned_worker);
                        }
                        fposix_socket::DatagramSocketRequest::Reopen {
                            options,
                            object_request,
                            control_handle: _,
                        } => {
                            let _ = object_request;
                            todo!("https://fxbug.dev/77623: options={:?}", options);
                        }
                        fposix_socket::DatagramSocketRequest::CloseDeprecated { responder } => {
                            let () = self.make_handler().await.close();
                            responder_send!(responder, zx::Status::OK.into_raw());
                            return Ok(());
                        }
                        fposix_socket::DatagramSocketRequest::Close { responder } => {
                            let () = self.make_handler().await.close();
                            responder_send!(responder, &mut Ok(()));
                            return Ok(());
                        }
                        fposix_socket::DatagramSocketRequest::SyncDeprecated { responder } => {
                            responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw());
                        }
                        fposix_socket::DatagramSocketRequest::Sync { responder } => {
                            responder_send!(
                                responder,
                                &mut Err(zx::Status::NOT_SUPPORTED.into_raw())
                            );
                        }
                        fposix_socket::DatagramSocketRequest::GetAttr { responder } => {
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
                        fposix_socket::DatagramSocketRequest::SetAttr {
                            flags: _,
                            attributes: _,
                            responder,
                        } => {
                            responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw());
                        }
                        fposix_socket::DatagramSocketRequest::GetAttributes {
                            query,
                            responder,
                        } => {
                            let _ = responder;
                            todo!("https://fxbug.dev/77623: query={:?}", query);
                        }
                        fposix_socket::DatagramSocketRequest::UpdateAttributes {
                            attributes,
                            responder,
                        } => {
                            let _ = responder;
                            todo!("https://fxbug.dev/77623: attributes={:?}", attributes);
                        }
                        fposix_socket::DatagramSocketRequest::Bind { addr, responder } => {
                            responder_send!(responder, &mut self.make_handler().await.bind(addr));
                        }
                        fposix_socket::DatagramSocketRequest::QueryFilesystem { responder } => {
                            responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw(), None);
                        }
                        fposix_socket::DatagramSocketRequest::GetSockName { responder } => {
                            responder_send!(
                                responder,
                                &mut self.make_handler().await.get_sock_name()
                            );
                        }
                        fposix_socket::DatagramSocketRequest::GetPeerName { responder } => {
                            responder_send!(
                                responder,
                                &mut self.make_handler().await.get_peer_name()
                            );
                        }
                        fposix_socket::DatagramSocketRequest::Shutdown { mode, responder } => {
                            responder_send!(
                                responder,
                                &mut self.make_handler().await.shutdown(mode)
                            )
                        }
                        fposix_socket::DatagramSocketRequest::RecvMsg {
                            want_addr,
                            data_len,
                            want_control: _,
                            flags: _,
                            responder,
                        } => {
                            // TODO(brunodalbo) handle control and flags
                            responder_send!(
                                responder,
                                &mut self
                                    .make_handler()
                                    .await
                                    .recv_msg(want_addr, data_len as usize)
                            );
                        }
                        fposix_socket::DatagramSocketRequest::SendMsg {
                            addr,
                            data,
                            control: _,
                            flags: _,
                            responder,
                        } => {
                            // TODO(https://fxbug.dev/21106): handle control.
                            responder_send!(
                                responder,
                                &mut self
                                    .make_handler()
                                    .await
                                    .send_msg(addr.map(|addr| *addr), data)
                            );
                        }
                        fposix_socket::DatagramSocketRequest::GetFlags { responder } => {
                            responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw(), 0);
                        }
                        fposix_socket::DatagramSocketRequest::SetFlags { flags: _, responder } => {
                            responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw());
                        }
                        fposix_socket::DatagramSocketRequest::GetInfo { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetTimestamp { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetTimestamp2 { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetTimestamp {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetTimestamp2 {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetError { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetSendBuffer {
                            value_bytes: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetSendBuffer { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetReceiveBuffer {
                            value_bytes: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetReceiveBuffer { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetReuseAddress {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetReuseAddress { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetReusePort {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetReusePort { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetAcceptConn { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetBindToDevice {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetBindToDevice { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetBroadcast {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetBroadcast { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetKeepAlive {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetKeepAlive { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetLinger {
                            linger: _,
                            length_secs: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetLinger { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetOutOfBandInline {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetOutOfBandInline { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetNoCheck {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetNoCheck { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpv6Only {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpv6Only { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpv6TrafficClass {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpv6TrafficClass { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpv6MulticastInterface {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpv6MulticastInterface {
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpv6UnicastHops {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpv6UnicastHops { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpv6MulticastHops {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpv6MulticastHops {
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpv6MulticastLoopback {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpv6MulticastLoopback {
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpTtl { value: _, responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpTtl { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpMulticastTtl {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpMulticastTtl { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpMulticastInterface {
                            iface: _,
                            address: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpMulticastInterface {
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpMulticastLoopback {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpMulticastLoopback {
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpTypeOfService {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpTypeOfService { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::AddIpMembership {
                            membership: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::DropIpMembership {
                            membership: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::AddIpv6Membership {
                            membership: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::DropIpv6Membership {
                            membership: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpv6ReceiveTrafficClass {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpv6ReceiveTrafficClass {
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpReceiveTypeOfService {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpReceiveTypeOfService {
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::SetIpPacketInfo {
                            value: _,
                            responder,
                        } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                        fposix_socket::DatagramSocketRequest::GetIpPacketInfo { responder } => {
                            responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                        }
                    }
                }
                Err(err) => {
                    let () = self.make_handler().await.close();
                    return Err(err);
                }
            }
        }
        // The loop breaks as the client side of the channel has been dropped,
        // need to treat that as an implicit close request as well.
        let () = self.make_handler().await.close();
        Ok(())
    }
}

pub(crate) trait RequestHandlerDispatcher<I, T>:
    EventDispatcher + AsRef<SocketCollectionPair<T>> + AsMut<SocketCollectionPair<T>>
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
    D: EventDispatcher,
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
    LockableContext<Dispatcher = <Self as RequestHandlerContext<I, T>>::Dispatcher>
where
    I: IpExt,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
    T: Transport<I>,
{
    type Dispatcher: RequestHandlerDispatcher<I, T>;
}

impl<I, T, C> RequestHandlerContext<I, T> for C
where
    I: IpExt,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
    T: Transport<I>,
    C: LockableContext,
    C::Dispatcher: RequestHandlerDispatcher<I, T>,
{
    type Dispatcher = C::Dispatcher;
}

struct RequestHandler<'a, I, T, C: LockableContext> {
    ctx: <C as Lockable<'a, Ctx<C::Dispatcher>>>::Guard,
    binding_id: usize,
    rights: u32,
    _marker: PhantomData<(I, T)>,
}

impl<'a, I, T, C> RequestHandler<'a, I, T, C>
where
    I: SocketCollectionIpExt<T> + IpExt + IpSockAddrExt,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
    T: Transport<I>,
    C: RequestHandlerContext<I, T>,
{
    fn describe(&self) -> Option<fio::NodeInfo> {
        self.get_state()
            .peer_event
            .duplicate_handle(zx::Rights::BASIC)
            .map(|peer| fio::NodeInfo::DatagramSocket(fio::DatagramSocket { event: peer }))
            .ok()
    }

    fn get_state(&self) -> &BindingData<I, T> {
        I::get_collection(&self.ctx.dispatcher).binding_data.get(self.binding_id).unwrap()
    }

    fn get_state_mut(&mut self) -> &mut BindingData<I, T> {
        I::get_collection_mut(&mut self.ctx.dispatcher)
            .binding_data
            .get_mut(self.binding_id)
            .unwrap()
    }
}

impl<'a, I, T, C> RequestHandler<'a, I, T, C>
where
    I: SocketCollectionIpExt<T> + IpExt + IpSockAddrExt,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
    T: TransportState<I, Ctx<<C as RequestHandlerContext<I, T>>::Dispatcher>>,
    C: RequestHandlerContext<I, T>,
    Ctx<<C as RequestHandlerContext<I, T>>::Dispatcher>: TransportIpContext<I>,
{
    /// Handles a [POSIX socket connect request].
    ///
    /// [POSIX socket connect request]: fposix_socket::DatagramSocketRequest::Connect
    fn connect(mut self, addr: fnet::SocketAddress) -> Result<(), fposix::Errno> {
        let sockaddr = I::SocketAddress::from_sock_addr(addr)?;
        trace!("connect sockaddr: {:?}", sockaddr);
        let remote_port =
            T::RemoteIdentifier::from_u16(sockaddr.port()).ok_or(fposix::Errno::Econnrefused)?;
        let remote_addr = sockaddr.get_specified_addr().ok_or(fposix::Errno::Einval)?;

        let (local_addr, local_port) = match self.get_state().info.state {
            SocketState::Unbound => {
                // do nothing, we're already unbound.
                // return None for local_addr and local_port.
                (None, None)
            }
            SocketState::BoundListen { listener_id } => {
                // if we're bound to a listen mode, we need to remove the
                // listener, and retrieve the bound local addr and port.
                let (local_ip, local_port) = T::remove_listener(self.ctx.deref_mut(), listener_id);
                // also remove from the EventLoop context:
                assert_ne!(
                    I::get_collection_mut(&mut self.ctx.dispatcher).listeners.remove(&listener_id),
                    None
                );

                (local_ip, Some(local_port))
            }
            SocketState::BoundConnect { conn_id, .. } => {
                // if we're bound to a connect mode, we need to remove the
                // connection, and retrieve the bound local addr and port.
                let (local_ip, local_port, _, _): (
                    _,
                    _,
                    SpecifiedAddr<I::Addr>,
                    T::RemoteIdentifier,
                ) = T::remove_conn(self.ctx.deref_mut(), conn_id);
                // also remove from the EventLoop context:
                assert_ne!(
                    I::get_collection_mut(&mut self.ctx.dispatcher).conns.remove(&conn_id),
                    None
                );
                (Some(local_ip), Some(local_port))
            }
        };

        let conn_id = T::create_connection(
            self.ctx.deref_mut(),
            local_addr,
            local_port,
            remote_addr,
            remote_port,
        )
        .map_err(IntoErrno::into_errno)?;

        self.get_state_mut().info.state =
            SocketState::BoundConnect { conn_id, shutdown_read: false, shutdown_write: false };
        assert_eq!(
            I::get_collection_mut(&mut self.ctx.dispatcher).conns.insert(&conn_id, self.binding_id),
            None
        );
        Ok(())
    }

    /// Handles a [POSIX socket bind request].
    ///
    /// [POSIX socket bind request]: fposix_socket::DatagramSocketRequest::Bind
    fn bind(mut self, addr: fnet::SocketAddress) -> Result<(), fposix::Errno> {
        let sockaddr = I::SocketAddress::from_sock_addr(addr)?;
        trace!("bind sockaddr: {:?}", sockaddr);
        if self.get_state().info.state.is_bound() {
            return Err(fposix::Errno::Ealready);
        }
        let local_addr = sockaddr.get_specified_addr();
        let local_port = T::LocalIdentifier::from_u16(sockaddr.port());

        let listener_id = T::create_listener(self.ctx.deref_mut(), local_addr, local_port)
            .map_err(IntoErrno::into_errno)?;
        self.get_state_mut().info.state = SocketState::BoundListen { listener_id };
        assert_eq!(
            I::get_collection_mut(&mut self.ctx.dispatcher)
                .listeners
                .insert(&listener_id, self.binding_id),
            None
        );
        Ok(())
    }

    /// Handles a [POSIX socket get_sock_name request].
    ///
    /// [POSIX socket get_sock_name request]: fposix_socket::DatagramSocketRequest::GetSockName
    fn get_sock_name(self) -> Result<fnet::SocketAddress, fposix::Errno> {
        match self.get_state().info.state {
            SocketState::Unbound { .. } => {
                return Err(fposix::Errno::Enotsock);
            }
            SocketState::BoundConnect { conn_id, .. } => {
                let (local_ip, local_port, _, _): (
                    _,
                    _,
                    SpecifiedAddr<I::Addr>,
                    T::RemoteIdentifier,
                ) = T::get_conn_info(self.ctx.deref(), conn_id);
                Ok(I::SocketAddress::new(*local_ip, local_port.into()).into_sock_addr())
            }
            SocketState::BoundListen { listener_id } => {
                let (local_ip, local_port) = T::get_listener_info(self.ctx.deref(), listener_id);
                let local_ip = local_ip.map_or(I::UNSPECIFIED_ADDRESS, |local_ip| *local_ip);
                Ok(I::SocketAddress::new(local_ip, local_port.into()).into_sock_addr())
            }
        }
    }

    /// Handles a [POSIX socket get_peer_name request].
    ///
    /// [POSIX socket get_peer_name request]: fposix_socket::DatagramSocketRequest::GetPeerName
    fn get_peer_name(self) -> Result<fnet::SocketAddress, fposix::Errno> {
        match self.get_state().info.state {
            SocketState::Unbound { .. } => {
                return Err(fposix::Errno::Enotsock);
            }
            SocketState::BoundListen { .. } => {
                return Err(fposix::Errno::Enotconn);
            }
            SocketState::BoundConnect { conn_id, .. } => {
                let (_, _, remote_ip, remote_port): (
                    SpecifiedAddr<I::Addr>,
                    T::LocalIdentifier,
                    _,
                    _,
                ) = T::get_conn_info(self.ctx.deref(), conn_id);
                Ok(I::SocketAddress::new(*remote_ip, remote_port.into()).into_sock_addr())
            }
        }
    }

    fn close_core(&mut self) {
        match self.get_state().info.state {
            SocketState::Unbound => (), // nothing to do
            SocketState::BoundListen { listener_id } => {
                // remove from bindings:
                assert_ne!(
                    I::get_collection_mut(&mut self.ctx.dispatcher).listeners.remove(&listener_id),
                    None
                );
                // remove from core:
                let _: (Option<SpecifiedAddr<I::Addr>>, T::LocalIdentifier) =
                    T::remove_listener(self.ctx.deref_mut(), listener_id);
            }
            SocketState::BoundConnect { conn_id, .. } => {
                // remove from bindings:
                assert_ne!(
                    I::get_collection_mut(&mut self.ctx.dispatcher).conns.remove(&conn_id),
                    None
                );
                // remove from core:
                let _: (
                    SpecifiedAddr<I::Addr>,
                    T::LocalIdentifier,
                    SpecifiedAddr<I::Addr>,
                    T::RemoteIdentifier,
                ) = T::remove_conn(self.ctx.deref_mut(), conn_id);
            }
        }
        self.get_state_mut().info.state = SocketState::Unbound;
    }

    fn close(mut self) {
        let inner = self.get_state_mut();
        if inner.ref_count == 1 {
            // always make sure the socket is closed with core.
            self.close_core();
            assert_matches::assert_matches!(
                I::get_collection_mut(&mut self.ctx.dispatcher)
                    .binding_data
                    .remove(self.binding_id),
                Some(BindingData {
                    local_event: _,
                    peer_event: _,
                    info: _,
                    available_data: _,
                    ref_count: 1,
                })
            );
        } else {
            inner.ref_count -= 1;
        }
    }

    fn need_rights(&self, required: u32) -> Result<(), fposix::Errno> {
        if self.rights & required == required {
            Ok(())
        } else {
            Err(fposix::Errno::Eperm)
        }
    }

    fn recv_msg(
        &mut self,
        want_addr: bool,
        data_len: usize,
    ) -> Result<
        (
            Option<Box<fnet::SocketAddress>>,
            Vec<u8>,
            fposix_socket::DatagramSocketRecvControlData,
            u32,
        ),
        fposix::Errno,
    > {
        let () = self.need_rights(fio::OPEN_RIGHT_READABLE)?;
        let state = self.get_state_mut();
        let available = if let Some(front) = state.available_data.pop_front() {
            front
        } else {
            if let SocketState::BoundConnect { shutdown_read, .. } = state.info.state {
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
                I::SocketAddress::new(available.source_addr, available.source_port)
                    .into_sock_addr(),
            ))
        } else {
            None
        };
        let mut data = available.data;
        let truncated = data.len().saturating_sub(data_len);
        data.truncate(data_len);

        if state.available_data.is_empty() {
            if let Err(e) = state.local_event.signal_peer(ZXSIO_SIGNAL_INCOMING, zx::Signals::NONE)
            {
                error!("socket failed to signal peer: {:?}", e);
            }
        }
        Ok((
            addr,
            data,
            fposix_socket::DatagramSocketRecvControlData::EMPTY,
            truncated.try_into().unwrap_or(u32::MAX),
        ))
    }
}

impl<'a, I, T, C> RequestHandler<'a, I, T, C>
where
    I: SocketCollectionIpExt<T> + IpExt + IpSockAddrExt,
    T: Transport<Ipv4>,
    T: Transport<Ipv6>,
    T: TransportState<I, Ctx<<C as RequestHandlerContext<I, T>>::Dispatcher>>,
    T: BufferTransportState<I, Buf<Vec<u8>>, Ctx<<C as RequestHandlerContext<I, T>>::Dispatcher>>,
    C: RequestHandlerContext<I, T>,
    Ctx<<C as RequestHandlerContext<I, T>>::Dispatcher>: TransportIpContext<I>,
{
    fn send_msg(
        &mut self,
        addr: Option<fnet::SocketAddress>,
        data: Vec<u8>,
    ) -> Result<i64, fposix::Errno> {
        let () = self.need_rights(fio::OPEN_RIGHT_WRITABLE)?;
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
        match self.get_state().info.state {
            SocketState::Unbound => {
                // TODO(brunodalbo) if destination address is set, we should
                // auto-bind here (check POSIX compliance).
                Err(fposix::Errno::Edestaddrreq)
            }
            SocketState::BoundConnect { conn_id, shutdown_write, .. } => {
                if shutdown_write {
                    return Err(fposix::Errno::Epipe);
                }
                match remote {
                    Some((addr, port)) => {
                        // Caller specified a remote socket address; use
                        // stateless send using the local address and port
                        // in `conn_id`.
                        let (local_ip, local_port, _, _): (
                            _,
                            _,
                            SpecifiedAddr<I::Addr>,
                            T::RemoteIdentifier,
                        ) = T::get_conn_info(self.ctx.deref(), conn_id);
                        T::send(
                            self.ctx.deref_mut(),
                            Some(local_ip),
                            Some(local_port),
                            addr,
                            port,
                            body,
                        )
                        .map_err(|(_body, err)| err.into_errno())
                    }
                    None => {
                        // Caller did not specify a remote socket address; just
                        // use the existing conn.
                        T::send_conn(self.ctx.deref_mut(), conn_id, body)
                            .map_err(|(_body, err)| err.into_errno())
                    }
                }
            }
            SocketState::BoundListen { listener_id } => match remote {
                Some((addr, port)) => {
                    T::send_listener(self.ctx.deref_mut(), listener_id, None, addr, port, body)
                        .map_err(|(_body, err)| err.into_errno())
                }
                None => Err(fposix::Errno::Edestaddrreq),
            },
        }
        .map(|()| len)
    }

    fn shutdown(mut self, how: fposix_socket::ShutdownMode) -> Result<(), fposix::Errno> {
        // Only "connected" sockets can be shutdown.
        if let SocketState::BoundConnect { ref mut shutdown_read, ref mut shutdown_write, .. } =
            self.get_state_mut().info.state
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
                if let Err(e) = self
                    .get_state()
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
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::{
        endpoints::{Proxy, ServerEnd},
        AsyncChannel,
    };
    use fuchsia_async as fasync;
    use fuchsia_zircon::{self as zx, AsHandleRef};
    use futures::StreamExt;

    use crate::bindings::integration_tests::{
        test_ep_name, StackSetupBuilder, TestSetup, TestSetupBuilder, TestStack,
    };
    use crate::bindings::socket::testutil::TestSockAddr;
    use net_types::ip::{Ip, IpAddress};

    async fn prepare_test<A: TestSockAddr>(
        proto: fposix_socket::DatagramSocketProtocol,
    ) -> (TestSetup, fposix_socket::DatagramSocketProxy) {
        let mut t = TestSetupBuilder::new()
            .add_endpoint()
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint(test_ep_name(1), Some(A::config_addr_subnet())),
            )
            .build()
            .await
            .unwrap();
        let proxy = get_socket::<A>(t.get(0), proto).await;
        (t, proxy)
    }

    async fn get_socket<A: TestSockAddr>(
        test_stack: &mut TestStack,
        proto: fposix_socket::DatagramSocketProtocol,
    ) -> fposix_socket::DatagramSocketProxy {
        let socket_provider = test_stack.connect_socket_provider().unwrap();
        let socket = socket_provider
            .datagram_socket(A::DOMAIN, proto)
            .await
            .unwrap()
            .expect("Socket succeeds");
        fposix_socket::DatagramSocketProxy::new(
            fasync::Channel::from_channel(socket.into_channel()).unwrap(),
        )
    }

    async fn get_socket_and_event<A: TestSockAddr>(
        test_stack: &mut TestStack,
        proto: fposix_socket::DatagramSocketProtocol,
    ) -> (fposix_socket::DatagramSocketProxy, zx::EventPair) {
        let ctlr = get_socket::<A>(test_stack, proto).await;
        let node_info = ctlr.describe().await.expect("Socked describe succeeds");
        let event = match node_info {
            fio::NodeInfo::DatagramSocket(e) => e.event,
            _ => panic!("Got wrong describe response for UDP socket"),
        };
        (ctlr, event)
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
        let (_t, proxy) = prepare_test::<A>(proto).await;

        // Pass a bad domain.
        let res = proxy
            .connect(&mut A::DifferentDomain::create(A::DifferentDomain::LOCAL_ADDR, 1010))
            .await
            .unwrap()
            .expect_err("connect fails");
        assert_eq!(res, fposix::Errno::Eafnosupport);

        // Pass an unspecified remote address.
        let res = proxy
            .connect(&mut A::create(<A::AddrType as IpAddress>::Version::UNSPECIFIED_ADDRESS, 1010))
            .await
            .unwrap()
            .expect_err("connect fails");
        assert_eq!(res, fposix::Errno::Einval);

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
        let (_t, proxy) = prepare_test::<A>(proto).await;
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

    async fn bind<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol) {
        let (mut t, socket) = prepare_test::<A>(proto).await;
        let stack = t.get(0);
        // Can bind to local address.
        let () =
            socket.bind(&mut A::create(A::LOCAL_ADDR, 200)).await.unwrap().expect("bind succeeds");

        // Can't bind again (to another port).
        let res =
            socket.bind(&mut A::create(A::LOCAL_ADDR, 201)).await.unwrap().expect_err("bind fails");
        assert_eq!(res, fposix::Errno::Ealready);

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
        let (_t, socket) = prepare_test::<A>(proto).await;
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
        let socket = socket_provider
            .datagram_socket(domain, proto)
            .await
            .unwrap()
            .expect("Socket call succeeds")
            .into_proxy()
            .unwrap();
        let info = socket.describe().await.expect("Describe call succeeds");
        match info {
            fio::NodeInfo::DatagramSocket(_) => (),
            info => panic!(
                "Socket Describe call did not return Node of type Socket, got {:?} instead",
                info
            ),
        }
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

    async fn socket_clone(
        socket: &fposix_socket::DatagramSocketProxy,
        flags: u32,
    ) -> Result<fposix_socket::DatagramSocketProxy, Error> {
        let (server, client) = zx::Channel::create()?;
        socket.clone(flags, ServerEnd::from(server))?;
        let channel = AsyncChannel::from_channel(client)?;
        Ok(fposix_socket::DatagramSocketProxy::new(channel))
    }

    async fn clone<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol)
    where
        <A::AddrType as IpAddress>::Version: SocketCollectionIpExt<T>,
        T: Transport<Ipv4>,
        T: Transport<Ipv6>,
        T: Transport<<A::AddrType as IpAddress>::Version>,
        crate::bindings::BindingsDispatcher: AsRef<SocketCollectionPair<T>>,
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
        // Test for the OPEN_FLAG_DESCRIBE.
        let alice_cloned =
            socket_clone(&alice_socket, fio::CLONE_FLAG_SAME_RIGHTS | fio::OPEN_FLAG_DESCRIBE)
                .await
                .expect("cannot clone socket");
        let mut events = alice_cloned.take_event_stream();
        match events.next().await.expect("stream closed").expect("failed to decode") {
            fposix_socket::DatagramSocketEvent::OnOpen_ { s, info } => {
                assert_eq!(s, zx::sys::ZX_OK);
                let info = info.unwrap();
                match *info {
                    fio::NodeInfo::DatagramSocket(_) => (),
                    info => panic!(
                        "Socket Describe call did not return Node of type Socket, got {:?} instead",
                        info
                    ),
                }
            }
            fposix_socket::DatagramSocketEvent::OnConnectionInfo { info } => {
                match info.representation.expect("missing representation") {
                    fio::Representation::DatagramSocket(_) => (),
                    representation => panic!(
                        "Socket Describe call did not return Node of type Socket, got {:?} instead",
                        representation
                    ),
                }
            }
        }
        // describe() explicitly.
        let info = alice_cloned.describe().await.expect("Describe call succeeds");
        match info {
            fio::NodeInfo::DatagramSocket(_) => (),
            info => panic!(
                "Socket Describe call did not return Node of type Socket, got {:?} instead",
                info
            ),
        }

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
        let bob_cloned = socket_clone(&bob_socket, fio::CLONE_FLAG_SAME_RIGHTS)
            .await
            .expect("failed to clone socket");
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

        {
            let alice_readonly =
                socket_clone(&alice_socket, fio::OPEN_RIGHT_READABLE).await.unwrap();
            let bob_writeonly = socket_clone(&bob_cloned, fio::OPEN_RIGHT_WRITABLE).await.unwrap();
            // We shouldn't allow the following.
            expect_clone_invalid_args(&alice_readonly, fio::OPEN_RIGHT_WRITABLE).await;
            expect_clone_invalid_args(&bob_writeonly, fio::OPEN_RIGHT_READABLE).await;

            assert_eq!(
                alice_readonly
                    .send_msg(
                        Some(&mut A::create(A::LOCAL_ADDR, 200)),
                        &body,
                        fposix_socket::DatagramSocketSendControlData::EMPTY,
                        fposix_socket::SendMsgFlags::empty()
                    )
                    .await
                    .unwrap()
                    .expect_err("should not send_msg on a readonly socket"),
                fposix::Errno::Eperm,
            );

            assert_eq!(
                bob_writeonly
                    .recv_msg(false, 2048, false, fposix_socket::RecvMsgFlags::empty())
                    .await
                    .unwrap()
                    .expect_err("should not recv_msg on a writeonly socket"),
                fposix::Errno::Eperm,
            );

            assert_eq!(
                bob_writeonly
                    .send_msg(
                        Some(&mut A::create(A::LOCAL_ADDR, 200)),
                        &body,
                        fposix_socket::DatagramSocketSendControlData::EMPTY,
                        fposix_socket::SendMsgFlags::empty()
                    )
                    .await
                    .unwrap()
                    .expect("failed to send_msg on bob writeonly"),
                body.len() as i64
            );

            let alice_readonly_info = alice_readonly.describe().await.expect("failed to describe");
            let alice_readonly_event = match alice_readonly_info {
                fio::NodeInfo::DatagramSocket(e) => e.event,
                _ => panic!("Got wrong describe response for UDP socket"),
            };
            assert_eq!(
                fasync::OnSignals::new(&alice_readonly_event, ZXSIO_SIGNAL_INCOMING).await,
                Ok(ZXSIO_SIGNAL_INCOMING | ZXSIO_SIGNAL_OUTGOING)
            );

            let (from, data, _, truncated) = alice_readonly
                .recv_msg(true, 2048, false, fposix_socket::RecvMsgFlags::empty())
                .await
                .unwrap()
                .expect("failed to recv_msg on alice readonly");
            assert_eq!(&data[..], body);
            assert_eq!(truncated, 0);
            assert_eq!(from.map(|a| *a), Some(A::create(A::REMOTE_ADDR, 200)));
        }

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
                    let SocketCollection { binding_data, conns, listeners } =
                        <A::AddrType as IpAddress>::Version::get_collection(&ctx.dispatcher);
                    assert_matches::assert_matches!(
                        binding_data.iter().collect::<Vec<_>>()[..],
                        [_]
                    );
                    assert_matches::assert_matches!(conns.iter().collect::<Vec<_>>()[..], []);
                    assert_matches::assert_matches!(listeners.iter().collect::<Vec<_>>()[..], [_]);
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
                    let SocketCollection { binding_data, conns, listeners } =
                        <A::AddrType as IpAddress>::Version::get_collection(&ctx.dispatcher);
                    assert_matches::assert_matches!(
                        binding_data.iter().collect::<Vec<_>>()[..],
                        []
                    );
                    assert_matches::assert_matches!(conns.iter().collect::<Vec<_>>()[..], []);
                    assert_matches::assert_matches!(listeners.iter().collect::<Vec<_>>()[..], []);
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
        crate::bindings::BindingsDispatcher: AsRef<SocketCollectionPair<T>>,
    {
        // Make sure we cannot close twice from the same channel so that we
        // maintain the correct refcount.
        let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
        let test_stack = t.get(0);
        let socket = get_socket::<A>(test_stack, proto).await;
        let cloned = socket_clone(&socket, fio::CLONE_FLAG_SAME_RIGHTS).await.unwrap();
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
                let SocketCollection { binding_data, conns, listeners } =
                    <A::AddrType as IpAddress>::Version::get_collection(&ctx.dispatcher);
                assert_matches::assert_matches!(binding_data.iter().collect::<Vec<_>>()[..], [_]);
                assert_matches::assert_matches!(conns.iter().collect::<Vec<_>>()[..], []);
                assert_matches::assert_matches!(listeners.iter().collect::<Vec<_>>()[..], []);
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
                let SocketCollection { binding_data, conns, listeners } =
                    <A::AddrType as IpAddress>::Version::get_collection(&ctx.dispatcher);
                assert_matches::assert_matches!(binding_data.iter().collect::<Vec<_>>()[..], []);
                assert_matches::assert_matches!(conns.iter().collect::<Vec<_>>()[..], []);
                assert_matches::assert_matches!(listeners.iter().collect::<Vec<_>>()[..], []);
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
        crate::bindings::BindingsDispatcher: AsRef<SocketCollectionPair<T>>,
    {
        let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
        let test_stack = t.get(0);
        let cloned = {
            let socket = get_socket::<A>(test_stack, proto).await;
            socket_clone(&socket, fio::CLONE_FLAG_SAME_RIGHTS).await.unwrap()
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
                let SocketCollection { binding_data, conns, listeners } =
                    <A::AddrType as IpAddress>::Version::get_collection(&ctx.dispatcher);
                assert_matches::assert_matches!(binding_data.iter().collect::<Vec<_>>()[..], []);
                assert_matches::assert_matches!(conns.iter().collect::<Vec<_>>()[..], []);
                assert_matches::assert_matches!(listeners.iter().collect::<Vec<_>>()[..], []);
            })
            .await;
    }

    declare_tests!(implicit_close);

    async fn expect_clone_invalid_args(socket: &fposix_socket::DatagramSocketProxy, flags: u32) {
        let cloned = socket_clone(&socket, flags).await.unwrap();
        {
            let mut events = cloned.take_event_stream();
            if let Some(result) = events.next().await {
                match result.expect("failed to decode") {
                    fposix_socket::DatagramSocketEvent::OnOpen_ { s, .. } => {
                        assert_eq!(s, zx::sys::ZX_ERR_INVALID_ARGS);
                    }
                    fposix_socket::DatagramSocketEvent::OnConnectionInfo { .. } => {
                        assert!(false);
                    }
                }
            }
        }
        assert!(cloned.into_channel().unwrap().is_closed());
    }

    async fn invalid_clone_args<A: TestSockAddr, T>(proto: fposix_socket::DatagramSocketProtocol)
    where
        <A::AddrType as IpAddress>::Version: SocketCollectionIpExt<T>,
        T: Transport<Ipv4>,
        T: Transport<Ipv6>,
        T: Transport<<A::AddrType as IpAddress>::Version>,
        crate::bindings::BindingsDispatcher: AsRef<SocketCollectionPair<T>>,
    {
        let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
        let test_stack = t.get(0);
        let socket = get_socket::<A>(test_stack, proto).await;
        // conflicting flags
        expect_clone_invalid_args(&socket, fio::CLONE_FLAG_SAME_RIGHTS | fio::OPEN_RIGHT_READABLE)
            .await;
        // no remote
        expect_clone_invalid_args(&socket, fio::OPEN_FLAG_NO_REMOTE).await;
        // append
        expect_clone_invalid_args(&socket, fio::OPEN_FLAG_APPEND).await;
        // executable
        expect_clone_invalid_args(&socket, fio::OPEN_RIGHT_EXECUTABLE).await;
        let () = socket
            .close()
            .await
            .expect("FIDL error")
            .map_err(zx::Status::from_raw)
            .expect("close failed");

        // make sure we don't leak anything.
        test_stack
            .with_ctx(|ctx| {
                let SocketCollection { binding_data, conns, listeners } =
                    <A::AddrType as IpAddress>::Version::get_collection(&ctx.dispatcher);
                assert_matches::assert_matches!(binding_data.iter().collect::<Vec<_>>()[..], []);
                assert_matches::assert_matches!(conns.iter().collect::<Vec<_>>()[..], []);
                assert_matches::assert_matches!(listeners.iter().collect::<Vec<_>>()[..], []);
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
}
