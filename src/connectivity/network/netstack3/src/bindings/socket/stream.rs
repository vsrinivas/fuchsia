// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Stream sockets, primarily TCP sockets.

use std::{
    num::{NonZeroU16, NonZeroUsize},
    ops::DerefMut as _,
};

use assert_matches::assert_matches;
use fidl::{
    endpoints::{ControlHandle as _, RequestStream as _},
    HandleBased as _,
};
use fidl_fuchsia_io as fio;
use fidl_fuchsia_posix as fposix;
use fidl_fuchsia_posix_socket as fposix_socket;
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, Peered as _};
use futures::StreamExt as _;
use net_types::ip::{IpVersionMarker, Ipv4, Ipv6};
use net_types::SpecifiedAddr;
use packet::Buf;

use crate::bindings::{
    socket::{
        IntoErrno, IpSockAddrExt, SockAddr as _, ZXSIO_SIGNAL_CONNECTED, ZXSIO_SIGNAL_INCOMING,
    },
    LockableContext,
};

use netstack3_core::{
    ip::{socket::BufferIpSocketHandler, IpExt, TransportIpContext},
    transport::tcp::{
        buffer::RingBuffer,
        socket::{
            accept, bind, connect_bound, connect_unbound, create_socket, listen, AcceptError,
            BindError, BoundId, ConnectError, ConnectionId, ListenerId, SocketAddr,
            TcpNonSyncContext, TcpSyncContext, UnboundId,
        },
    },
    Ctx, SyncCtx,
};

#[derive(Debug, Clone, Copy)]
enum SocketId {
    Unbound(UnboundId),
    Bound(BoundId),
    Connection(ConnectionId),
    Listener(ListenerId),
}

pub(crate) trait SocketWorkerDispatcher:
    TcpNonSyncContext<NetstackEndBuffers = (), ClientEndBuffers = ()>
{
    /// Registers a newly created listener with its local zircon socket.
    ///
    /// # Panics
    /// Panics if `id` is already registered.
    fn register_listener(&mut self, id: ListenerId, socket: zx::Socket);
    /// Unregisters an existing listener when it is about to be closed.
    ///
    /// # Panics
    /// Panics if `id` is non-existent.
    fn unregister_listener(&mut self, id: ListenerId);
}

impl SocketWorkerDispatcher for crate::bindings::BindingsNonSyncCtxImpl {
    fn register_listener(&mut self, id: ListenerId, socket: zx::Socket) {
        assert_matches!(self.tcp_listeners.insert(id.into(), socket), None);
    }

    fn unregister_listener(&mut self, id: ListenerId) {
        assert_matches!(self.tcp_listeners.remove(id.into()), Some(_));
    }
}

impl TcpNonSyncContext for crate::bindings::BindingsNonSyncCtxImpl {
    type ReceiveBuffer = RingBuffer;
    type SendBuffer = RingBuffer;
    // TODO(https://fxbug.dev/104013): These are `()` for now but should be
    // changed to zircon sockets.
    type ClientEndBuffers = ();
    type NetstackEndBuffers = ();

    fn on_new_connection(&mut self, listener: ListenerId) {
        self.tcp_listeners
            .get(listener.into())
            .expect("invalid listener")
            .signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_INCOMING)
            .expect("failed to signal that the new connection is available");
    }

    fn new_passive_open_buffers() -> (Self::ReceiveBuffer, Self::SendBuffer, Self::ClientEndBuffers)
    {
        (RingBuffer::default(), RingBuffer::default(), ())
    }
}

struct SocketWorker<I: IpExt, C> {
    id: SocketId,
    ctx: C,
    // TODO(https://fxbug.dev/104976): We may be able to not store this in this
    // struct, if not, this should be an `Arc` to be explicit that it will be
    // shared with Core.
    local: zx::Socket,
    peer: zx::Socket,
    _marker: IpVersionMarker<I>,
}

impl<I: IpExt, C> SocketWorker<I, C> {
    fn new(id: SocketId, ctx: C) -> Result<Self, fposix::Errno> {
        let (local, peer) = zx::Socket::create(zx::SocketOpts::STREAM)
            .map_err(|_: zx::Status| fposix::Errno::Enobufs)?;
        Ok(Self { id, ctx, local, peer, _marker: Default::default() })
    }
}

pub(super) async fn spawn_worker<C>(
    domain: fposix_socket::Domain,
    proto: fposix_socket::StreamSocketProtocol,
    ctx: C,
    request_stream: fposix_socket::StreamSocketRequestStream,
) -> Result<(), fposix::Errno>
where
    C: LockableContext,
    C: Clone + Send + Sync + 'static,
    C::NonSyncCtx: SocketWorkerDispatcher,
{
    match (domain, proto) {
        (fposix_socket::Domain::Ipv4, fposix_socket::StreamSocketProtocol::Tcp) => {
            let id = {
                let mut guard = ctx.lock().await;
                let Ctx { sync_ctx, non_sync_ctx } = guard.deref_mut();
                SocketId::Unbound(create_socket::<Ipv4, _, _>(sync_ctx, non_sync_ctx))
            };
            let worker = SocketWorker::<Ipv4, C>::new(id, ctx.clone())?;
            Ok(worker.spawn(request_stream))
        }
        (fposix_socket::Domain::Ipv6, fposix_socket::StreamSocketProtocol::Tcp) => {
            let id = {
                let mut guard = ctx.lock().await;
                let Ctx { sync_ctx, non_sync_ctx } = guard.deref_mut();
                SocketId::Unbound(create_socket::<Ipv6, _, _>(sync_ctx, non_sync_ctx))
            };
            let worker = SocketWorker::<Ipv6, C>::new(id, ctx.clone())?;
            Ok(worker.spawn(request_stream))
        }
    }
}

impl IntoErrno for AcceptError {
    fn into_errno(self) -> fposix::Errno {
        match self {
            AcceptError::WouldBlock => fposix::Errno::Eagain,
        }
    }
}

impl IntoErrno for BindError {
    fn into_errno(self) -> fposix::Errno {
        match self {
            BindError::NoLocalAddr => fposix::Errno::Eaddrnotavail,
            BindError::NoPort => fposix::Errno::Eaddrinuse,
            BindError::Conflict => fposix::Errno::Eaddrinuse,
        }
    }
}

impl IntoErrno for ConnectError {
    fn into_errno(self) -> fposix::Errno {
        match self {
            ConnectError::NoRoute => fposix::Errno::Enetunreach,
            ConnectError::NoPort => fposix::Errno::Eaddrnotavail,
        }
    }
}

impl<I: IpSockAddrExt + IpExt, C> SocketWorker<I, C>
where
    C: LockableContext,
    C: Clone + Send + Sync + 'static,
    C::NonSyncCtx: SocketWorkerDispatcher,
    SyncCtx<C::NonSyncCtx>: TcpSyncContext<I, C::NonSyncCtx>
        + TransportIpContext<I, C::NonSyncCtx>
        + BufferIpSocketHandler<I, C::NonSyncCtx, Buf<Vec<u8>>>,
{
    fn spawn(mut self, mut request_stream: fposix_socket::StreamSocketRequestStream) {
        fasync::Task::spawn(async move {
            while let Some(Ok(request)) = request_stream.next().await {
                let mut guard = self.ctx.lock().await;
                let Ctx { sync_ctx, non_sync_ctx } = guard.deref_mut();
                match request {
                    fposix_socket::StreamSocketRequest::Bind { addr, responder } => {
                        let mut result = (|| match self.id {
                            SocketId::Unbound(unbound) => {
                                let addr = I::SocketAddress::from_sock_addr(addr)?;
                                let bound = bind::<I, _, _>(
                                    sync_ctx,
                                    non_sync_ctx,
                                    unbound,
                                    addr.addr(),
                                    NonZeroU16::new(addr.port()),
                                )
                                .map_err(IntoErrno::into_errno)?;
                                self.id = SocketId::Bound(bound);
                                Ok(())
                            }
                            SocketId::Bound(_)
                            | SocketId::Connection(_)
                            | SocketId::Listener(_) => Err(fposix::Errno::Einval),
                        })();
                        responder_send!(responder, &mut result);
                    }
                    fposix_socket::StreamSocketRequest::Connect { addr, responder } => {
                        let mut result = (|| {
                            let addr = I::SocketAddress::from_sock_addr(addr)?;
                            let ip =
                                SpecifiedAddr::new(addr.addr()).ok_or(fposix::Errno::Einval)?;
                            let port = NonZeroU16::new(addr.port()).ok_or(fposix::Errno::Einval)?;
                            match self.id {
                                SocketId::Bound(bound) => {
                                    let connected = connect_bound::<I, _, _>(
                                        sync_ctx,
                                        non_sync_ctx,
                                        bound,
                                        SocketAddr { ip, port },
                                        (),
                                    )
                                    .map_err(IntoErrno::into_errno)?;
                                    self.id = SocketId::Connection(connected);
                                    // TODO(https://fxbug.dev/104302): Signal
                                    // only if the connection is established.
                                    self.local
                                        .signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_CONNECTED)
                                        .expect(
                                            "failed to signal that the connection is established",
                                        );
                                    Ok(())
                                }
                                SocketId::Unbound(unbound) => {
                                    let connected = connect_unbound::<I, _, _>(
                                        sync_ctx,
                                        non_sync_ctx,
                                        unbound,
                                        SocketAddr { ip, port },
                                        (),
                                    )
                                    .map_err(IntoErrno::into_errno)?;
                                    self.id = SocketId::Connection(connected);
                                    self.local
                                        .signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_CONNECTED)
                                        .expect(
                                            "failed to signal that the connection is established",
                                        );
                                    Ok(())
                                }
                                SocketId::Listener(_) => Err(fposix::Errno::Einval),
                                SocketId::Connection(_) => Err(fposix::Errno::Eisconn),
                            }
                        })();
                        responder_send!(responder, &mut result);
                    }
                    fposix_socket::StreamSocketRequest::Describe { responder } => {
                        let socket = self
                            .peer
                            .duplicate_handle(zx::Rights::SAME_RIGHTS)
                            .expect("failed to duplicate the socket handle");
                        log::info!("describing: {:?}, zx::socket: {:?}", self.id, socket);
                        responder_send!(
                            responder,
                            &mut fio::NodeInfo::StreamSocket(fio::StreamSocket { socket })
                        );
                    }
                    fposix_socket::StreamSocketRequest::Describe2 { responder } => {
                        let socket = self
                            .peer
                            .duplicate_handle(zx::Rights::SAME_RIGHTS)
                            .expect("failed to duplicate the socket handle");
                        log::info!("describing: {:?}, zx::socket: {:?}", self.id, socket);
                        responder_send!(
                            responder,
                            fposix_socket::StreamSocketDescribe2Response {
                                socket: Some(socket),
                                ..fposix_socket::StreamSocketDescribe2Response::EMPTY
                            }
                        );
                    }
                    fposix_socket::StreamSocketRequest::Listen { backlog, responder } => {
                        let mut result = (|| match self.id {
                            SocketId::Bound(bound) => {
                                let backlog = NonZeroUsize::new(backlog as usize)
                                    .ok_or(fposix::Errno::Einval)?;
                                let listener =
                                    listen::<I, _, _>(sync_ctx, non_sync_ctx, bound, backlog);
                                non_sync_ctx.register_listener(
                                    listener,
                                    self.local
                                        .duplicate_handle(zx::Rights::SIGNAL_PEER)
                                        .expect("failed to duplicate handle"),
                                );
                                self.id = SocketId::Listener(listener);
                                Ok(())
                            }
                            SocketId::Unbound(_)
                            | SocketId::Connection(_)
                            | SocketId::Listener(_) => Err(fposix::Errno::Einval),
                        })();
                        responder_send!(responder, &mut result);
                    }
                    fposix_socket::StreamSocketRequest::Accept { want_addr, responder } => {
                        let mut result = (|| match self.id {
                            SocketId::Listener(listener) => {
                                let (accepted, addr) =
                                    accept::<I, _, _>(sync_ctx, non_sync_ctx, listener)
                                        .map(|(x, a, ())| {
                                            (
                                                x,
                                                I::SocketAddress::new(*a.ip, a.port.get())
                                                    .into_sock_addr(),
                                            )
                                        })
                                        .map_err(IntoErrno::into_errno)?;
                                let (client, request_stream) =
                                    fidl::endpoints::create_request_stream::<
                                        fposix_socket::StreamSocketMarker,
                                    >()
                                    .expect("failed to create new fidl endpoints");
                                let worker = SocketWorker::<I, C>::new(
                                    SocketId::Connection(accepted),
                                    self.ctx.clone(),
                                )
                                .expect("failed to create new worker");
                                worker
                                    .local
                                    .signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_CONNECTED)
                                    .expect("failed to signal that the connection is established");
                                worker.spawn(request_stream);
                                Ok((want_addr.then(|| Box::new(addr)), client))
                            }
                            SocketId::Unbound(_) | SocketId::Connection(_) | SocketId::Bound(_) => {
                                Err(fposix::Errno::Einval)
                            }
                        })();
                        responder_send!(responder, &mut result);
                    }
                    fposix_socket::StreamSocketRequest::Reopen {
                        rights_request,
                        object_request: _,
                        control_handle: _,
                    } => {
                        todo!("https://fxbug.dev/77623: rights_request={:?}", rights_request);
                    }
                    fposix_socket::StreamSocketRequest::Close { responder } => {
                        // TODO(https://fxbug.dev/103979): Remove socket from core.
                        request_stream.control_handle().shutdown();
                        responder_send!(responder, &mut Ok(()));
                        break;
                    }
                    fposix_socket::StreamSocketRequest::GetConnectionInfo { responder: _ } => {
                        todo!("https://fxbug.dev/77623");
                    }
                    fposix_socket::StreamSocketRequest::GetAttributes { query, responder: _ } => {
                        todo!("https://fxbug.dev/77623: query={:?}", query);
                    }
                    fposix_socket::StreamSocketRequest::UpdateAttributes {
                        payload,
                        responder: _,
                    } => {
                        todo!("https://fxbug.dev/77623: attributes={:?}", payload);
                    }
                    fposix_socket::StreamSocketRequest::Sync { responder } => {
                        responder_send!(responder, &mut Err(zx::Status::NOT_SUPPORTED.into_raw()));
                    }
                    fposix_socket::StreamSocketRequest::Clone {
                        flags: _,
                        object: _,
                        control_handle: _,
                    } => {
                        todo!()
                    }
                    fposix_socket::StreamSocketRequest::Clone2 {
                        request: _,
                        control_handle: _,
                    } => {
                        todo!()
                    }
                    fposix_socket::StreamSocketRequest::GetAttr { responder } => {
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
                    fposix_socket::StreamSocketRequest::SetAttr {
                        flags: _,
                        attributes: _,
                        responder,
                    } => {
                        responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw());
                    }
                    fposix_socket::StreamSocketRequest::GetFlags { responder } => {
                        responder_send!(
                            responder,
                            zx::Status::NOT_SUPPORTED.into_raw(),
                            fio::OpenFlags::empty()
                        );
                    }
                    fposix_socket::StreamSocketRequest::SetFlags { flags: _, responder } => {
                        responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw());
                    }
                    fposix_socket::StreamSocketRequest::Query { responder: _ } => {
                        todo!("https://fxbug.dev/105608: implement Query");
                    }
                    fposix_socket::StreamSocketRequest::QueryFilesystem { responder } => {
                        responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw(), None);
                    }
                    fposix_socket::StreamSocketRequest::SetReuseAddress { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetReuseAddress { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetError { responder } => {
                        // TODO(https://fxbug.dev/103982): Retrieve the error.
                        responder_send!(responder, &mut Ok(()));
                    }
                    fposix_socket::StreamSocketRequest::SetBroadcast { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetBroadcast { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetSendBuffer {
                        value_bytes: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetSendBuffer { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetReceiveBuffer {
                        value_bytes: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetReceiveBuffer { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetKeepAlive { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetKeepAlive { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetOutOfBandInline {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetOutOfBandInline { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetNoCheck { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetNoCheck { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetLinger {
                        linger: _,
                        length_secs: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetLinger { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetReusePort { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetReusePort { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetAcceptConn { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetBindToDevice { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetBindToDevice { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTimestampDeprecated {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTimestampDeprecated { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTimestamp { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTimestamp { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::Disconnect { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetSockName { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetPeerName { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::Shutdown { mode: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpTypeOfService {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpTypeOfService { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpTtl { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpTtl { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpPacketInfo { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpPacketInfo { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpReceiveTypeOfService {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpReceiveTypeOfService { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpReceiveTtl { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpReceiveTtl { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpMulticastInterface {
                        iface: _,
                        address: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpMulticastInterface { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpMulticastTtl {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpMulticastTtl { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpMulticastLoopback {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpMulticastLoopback { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::AddIpMembership {
                        membership: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::DropIpMembership {
                        membership: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::AddIpv6Membership {
                        membership: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::DropIpv6Membership {
                        membership: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpv6MulticastInterface {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpv6MulticastInterface { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpv6UnicastHops {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpv6UnicastHops { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpv6ReceiveHopLimit {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpv6ReceiveHopLimit { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpv6MulticastHops {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpv6MulticastHops { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpv6MulticastLoopback {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpv6MulticastLoopback { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpv6Only { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpv6Only { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpv6ReceiveTrafficClass {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpv6ReceiveTrafficClass {
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpv6TrafficClass {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpv6TrafficClass { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetIpv6ReceivePacketInfo {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetIpv6ReceivePacketInfo { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetInfo { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTcpNoDelay { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpNoDelay { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTcpMaxSegment {
                        value_bytes: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpMaxSegment { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTcpCork { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpCork { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTcpKeepAliveIdle {
                        value_secs: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpKeepAliveIdle { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTcpKeepAliveInterval {
                        value_secs: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpKeepAliveInterval { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTcpKeepAliveCount {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpKeepAliveCount { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTcpSynCount { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpSynCount { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTcpLinger {
                        value_secs: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpLinger { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTcpDeferAccept {
                        value_secs: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpDeferAccept { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTcpWindowClamp {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpWindowClamp { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpInfo { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTcpQuickAck { value: _, responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpQuickAck { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTcpCongestion {
                        value: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpCongestion { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::SetTcpUserTimeout {
                        value_millis: _,
                        responder,
                    } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                    fposix_socket::StreamSocketRequest::GetTcpUserTimeout { responder } => {
                        responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
                    }
                }
            }
            // TODO(https://fxbug.dev/103979): Remove socket from core.
            match self.id {
                SocketId::Unbound(_) | SocketId::Bound(_) | SocketId::Connection(_) => {}
                SocketId::Listener(listener) => {
                    self.ctx.lock().await.non_sync_ctx.unregister_listener(listener)
                }
            }
        })
        .detach()
    }
}
