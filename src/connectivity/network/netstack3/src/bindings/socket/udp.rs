// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! UDP socket bindings.

use std::convert::TryInto;
use std::marker::PhantomData;
use std::num::NonZeroU16;
use std::ops::{Deref, DerefMut};

use anyhow::{format_err, Error};
use fidl::endpoints::{RequestStream, ServerEnd};
use fidl::AsyncChannel;
use fidl_fuchsia_io::{self as fio, NodeInfo, NodeMarker};
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_posix::Errno;
use fidl_fuchsia_posix_socket::{
    self as psocket, DatagramSocketRequest, DatagramSocketRequestStream,
};
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, prelude::HandleBased, Peered};
use futures::{StreamExt, TryFutureExt};
use log::{debug, error, trace, warn};
use net_types::ip::{Ip, IpVersion, Ipv4, Ipv6};
use netstack3_core::{
    connect_udp, get_udp_conn_info, get_udp_listener_info, icmp::IcmpIpExt, listen_udp,
    remove_udp_conn, remove_udp_listener, send_udp, send_udp_conn, send_udp_listener, IdMap,
    IdMapCollection, UdpConnId, UdpEventDispatcher, UdpListenerId,
};
use packet::serialize::Buf;
use std::collections::VecDeque;

use crate::bindings::{context::InnerValue, BindingsDispatcher, LockedStackContext};

use super::{
    IntoErrno, IpSockAddrExt, SockAddr, SocketWorkerProperties, StackContext,
    ZXSIO_SIGNAL_INCOMING, ZXSIO_SIGNAL_OUTGOING,
};

/// Limits the number of messages that can be queued for an application to be
/// read before we start dropping packets.
// TODO(brunodalbo) move this to a buffer pool instead.
const MAX_OUTSTANDING_APPLICATION_MESSAGES: usize = 50;

#[derive(Default)]
pub(crate) struct UdpSocketCollection {
    v4: UdpSocketCollectionInner<Ipv4>,
    v6: UdpSocketCollectionInner<Ipv6>,
}

#[derive(Default)]
pub(crate) struct UdpSocketCollectionInner<I: Ip> {
    binding_data: IdMap<BindingData<I>>,
    /// Maps a `UdpConnId` to an index into the `binding_data` `IdMap`.
    conns: IdMapCollection<UdpConnId<I>, usize>,
    /// Maps a `UdpListenerId` to an index into the `binding_data` `IdMap`.
    listeners: IdMapCollection<UdpListenerId<I>, usize>,
}

impl<I: Ip> UdpSocketCollectionInner<I> {
    fn get_conn(&mut self, id: &UdpConnId<I>) -> &mut BindingData<I> {
        self.binding_data.get_mut(*self.conns.get(id).unwrap()).unwrap()
    }
    fn get_listener(&mut self, id: &UdpListenerId<I>) -> &mut BindingData<I> {
        self.binding_data.get_mut(*self.listeners.get(id).unwrap()).unwrap()
    }
}

/// Extension trait for [`Ip`] for UDP sockets operations.
pub(crate) trait UdpSocketIpExt: IpSockAddrExt + IcmpIpExt {
    fn get_collection<D: InnerValue<UdpSocketCollection>>(
        dispatcher: &D,
    ) -> &UdpSocketCollectionInner<Self>;
    fn get_collection_mut<D: InnerValue<UdpSocketCollection>>(
        dispatcher: &mut D,
    ) -> &mut UdpSocketCollectionInner<Self>;
}

impl UdpSocketIpExt for Ipv4 {
    fn get_collection<D: InnerValue<UdpSocketCollection>>(
        dispatcher: &D,
    ) -> &UdpSocketCollectionInner<Self> {
        &dispatcher.inner().v4
    }

    fn get_collection_mut<D: InnerValue<UdpSocketCollection>>(
        dispatcher: &mut D,
    ) -> &mut UdpSocketCollectionInner<Self> {
        &mut dispatcher.inner_mut().v4
    }
}

impl UdpSocketIpExt for Ipv6 {
    fn get_collection<D: InnerValue<UdpSocketCollection>>(
        dispatcher: &D,
    ) -> &UdpSocketCollectionInner<Self> {
        &dispatcher.inner().v6
    }

    fn get_collection_mut<D: InnerValue<UdpSocketCollection>>(
        dispatcher: &mut D,
    ) -> &mut UdpSocketCollectionInner<Self> {
        &mut dispatcher.inner_mut().v6
    }
}

// NOTE(brunodalbo) we implement UdpEventDispatcher for EventLoopInner in this
// module so it's closer to the rest of the UDP logic
impl<I: UdpSocketIpExt> UdpEventDispatcher<I> for BindingsDispatcher {
    fn receive_udp_from_conn(
        &mut self,
        conn: UdpConnId<I>,
        src_ip: I::Addr,
        src_port: NonZeroU16,
        body: &[u8],
    ) {
        let binding_data = I::get_collection_mut(self).get_conn(&conn);
        if let Err(e) = binding_data.receive_datagram(src_ip, src_port.get(), body) {
            error!("receive_udp_from_conn failed: {:?}", e);
        }
    }

    fn receive_udp_from_listen(
        &mut self,
        listener: UdpListenerId<I>,
        src_ip: I::Addr,
        _dst_ip: I::Addr,
        src_port: Option<NonZeroU16>,
        body: &[u8],
    ) {
        let binding_data = I::get_collection_mut(self).get_listener(&listener);
        if let Err(e) =
            binding_data.receive_datagram(src_ip, src_port.map(|p| p.get()).unwrap_or(0), body)
        {
            error!("receive_udp_from_conn failed: {:?}", e);
        }
    }
}

/// Worker that serves the fuchsia.posix.socket.Control FIDL.
struct UdpSocketWorker<I: UdpSocketIpExt, C: StackContext> {
    ctx: C,
    id: usize,
    rights: u32,
    _marker: PhantomData<I>,
}

#[derive(Debug)]
struct AvailableMessage<A> {
    source_addr: A,
    source_port: u16,
    data: Vec<u8>,
}

/// Internal state of a [`UdpSocketWorker`].
#[derive(Debug)]
struct BindingData<I: Ip> {
    local_event: zx::EventPair,
    peer_event: zx::EventPair,
    info: SocketControlInfo<I>,
    available_data: VecDeque<AvailableMessage<I::Addr>>,
    ref_count: usize,
}

impl<I: Ip> BindingData<I> {
    fn receive_datagram(&mut self, addr: I::Addr, port: u16, body: &[u8]) -> Result<(), Error> {
        if self.available_data.len() >= MAX_OUTSTANDING_APPLICATION_MESSAGES {
            return Err(format_err!("UDP application buffers are full"));
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
pub struct SocketControlInfo<I: Ip> {
    properties: SocketWorkerProperties,
    state: SocketState<I>,
}

/// Possible states for a UDP socket.
#[derive(Debug)]
enum SocketState<I: Ip> {
    Unbound,
    BoundListen { listener_id: UdpListenerId<I> },
    BoundConnect { conn_id: UdpConnId<I>, shutdown_read: bool, shutdown_write: bool },
}

impl<I: Ip> SocketState<I> {
    fn is_bound(&self) -> bool {
        match self {
            SocketState::Unbound => false,
            SocketState::BoundListen { .. } | SocketState::BoundConnect { .. } => true,
        }
    }
}

pub(crate) trait UdpStackContext<I: UdpSocketIpExt>: StackContext
where
    <Self as StackContext>::Dispatcher: UdpEventDispatcher<I> + InnerValue<UdpSocketCollection>,
{
}

impl<T, I> UdpStackContext<I> for T
where
    I: UdpSocketIpExt,
    T: StackContext,
    T::Dispatcher: UdpEventDispatcher<I> + InnerValue<UdpSocketCollection>,
{
}

pub(super) fn spawn_worker<C>(
    version: IpVersion,
    ctx: C,
    events: psocket::DatagramSocketRequestStream,
    properties: SocketWorkerProperties,
) -> Result<(), Errno>
where
    C: UdpStackContext<Ipv4> + UdpStackContext<Ipv6>,
    C::Dispatcher:
        InnerValue<UdpSocketCollection> + UdpEventDispatcher<Ipv4> + UdpEventDispatcher<Ipv6>,
{
    match version {
        IpVersion::V4 => UdpSocketWorker::<Ipv4, C>::spawn(ctx, properties, events),
        IpVersion::V6 => UdpSocketWorker::<Ipv6, C>::spawn(ctx, properties, events),
    }
}

impl<I: UdpSocketIpExt, C: UdpStackContext<I>> UdpSocketWorker<I, C>
where
    <C as StackContext>::Dispatcher: InnerValue<UdpSocketCollection> + UdpEventDispatcher<I>,
{
    /// Starts servicing events from the provided event stream.
    fn spawn(
        ctx: C,
        properties: SocketWorkerProperties,
        events: psocket::DatagramSocketRequestStream,
    ) -> Result<(), Errno> {
        let (local_event, peer_event) = zx::EventPair::create().map_err(|_| Errno::Enobufs)?;
        // signal peer that OUTGOING is available.
        // TODO(brunodalbo): We're currently not enforcing any sort of
        // flow-control for outgoing UDP datagrams. That'll get fixed once we
        // limit the number of in flight datagrams per socket (i.e. application
        // buffers).
        if let Err(e) = local_event.signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_OUTGOING) {
            error!("UDP socket failed to signal peer: {:?}", e);
        }
        fasync::Task::spawn(
            async move {
                let id = {
                    let mut locked = ctx.lock().await;
                    I::get_collection_mut(locked.dispatcher_mut())
                        .binding_data
                        .push(BindingData::<I>::new(local_event, peer_event, properties))
                };
                let worker = Self {
                    ctx,
                    id,
                    rights: fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                    _marker: PhantomData,
                };

                worker.handle_stream(events).await
            }
            // When the closure above finishes, that means `self` goes out
            // of scope and is dropped, meaning that the event stream's
            // underlying channel is closed.
            // If any errors occured as a result of the closure, we just log
            // them.
            .unwrap_or_else(|e: fidl::Error| error!("UDP socket control request error: {:?}", e)),
        )
        .detach();
        Ok(())
    }

    async fn clone(&self) -> UdpSocketWorker<I, C> {
        let mut handler = self.make_handler().await;
        let state = handler.get_state_mut();
        state.ref_count += 1;
        Self { ctx: self.ctx.clone(), id: self.id, rights: self.rights, _marker: PhantomData }
    }

    // Starts servicing a [Clone request](psocket::DatagramSocketRequest::Clone).
    fn clone_spawn(
        &self,
        flags: u32,
        object: ServerEnd<NodeMarker>,
        mut worker: UdpSocketWorker<I, C>,
    ) {
        fasync::Task::spawn(
            async move {
                let channel = AsyncChannel::from_channel(object.into_channel())
                    .expect("failed to create async channel");
                let events = DatagramSocketRequestStream::from_channel(channel);
                let control_handle = events.control_handle();
                let send_on_open = |status: i32, info: Option<&mut NodeInfo>| {
                    if let Err(e) = control_handle.send_on_open_(status, info) {
                        error!("failed to send OnOpen event with status ({}): {}", status, e);
                    }
                };
                // Datagram sockets don't understand the following flags.
                let append_no_remote =
                    flags & fio::OPEN_FLAG_APPEND != 0 || flags & fio::OPEN_FLAG_NO_REMOTE != 0;
                // Datagram sockets are neither mountable nor executable.
                let admin_executable =
                    flags & fio::OPEN_RIGHT_ADMIN != 0 || flags & fio::OPEN_RIGHT_EXECUTABLE != 0;
                // Cannot specify CLONE_FLAGS_SAME_RIGHTS together with OPEN_RIGHT_* flags.
                let conflicting_rights = flags & fio::CLONE_FLAG_SAME_RIGHTS != 0
                    && (flags & fio::OPEN_RIGHT_READABLE != 0
                        || flags & fio::OPEN_RIGHT_WRITABLE != 0);
                // If CLONE_FLAG_SAME_RIGHTS is not set, then use the intersection of the
                // inherited rights and the newly specified rights.
                let new_rights = flags & (fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE);
                let more_rights_than_original = new_rights & (!worker.rights) > 0;
                if flags & fio::CLONE_FLAG_SAME_RIGHTS == 0 && !more_rights_than_original {
                    worker.rights &= new_rights;
                }

                if append_no_remote
                    || admin_executable
                    || conflicting_rights
                    || more_rights_than_original
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
            .unwrap_or_else(|e: fidl::Error| error!("UDP socket control request error: {:?}", e)),
        )
        .detach();
    }

    async fn make_handler(&self) -> RequestHandler<'_, I, C> {
        let ctx = self.ctx.lock().await;
        RequestHandler { ctx, binding_id: self.id, rights: self.rights, _marker: PhantomData }
    }

    /// Handles [a stream of POSIX socket requests].
    ///
    /// Returns when getting the first `Close` request.
    ///
    /// [a stream of POSIX socket requests]: psocket::DatagramSocketRequestStream
    async fn handle_stream(
        self,
        mut events: DatagramSocketRequestStream,
    ) -> Result<(), fidl::Error> {
        // We need to early return here to avoid `Close` requests being received
        // on the same channel twice causing the incorrect decrease of refcount
        // as now the bindings data are potentially shared by several distinct
        // control channels.
        while let Some(event) = events.next().await {
            match event {
                Ok(req) => {
                    match req {
                        psocket::DatagramSocketRequest::Describe { responder } => {
                            // If the call to duplicate_handle fails, we have no choice but to drop the
                            // responder and close the channel, since Describe must be infallible.
                            if let Some(mut info) = self.make_handler().await.describe() {
                                responder_send!(responder, &mut info);
                            }
                        }
                        psocket::DatagramSocketRequest::Connect { addr: _, responder } => {
                            // NB: Netstack3 is not load-bearing enough to justify
                            // maintaining two implementations.
                            responder
                                .control_handle()
                                .shutdown_with_epitaph(zx::Status::NOT_SUPPORTED);
                        }
                        psocket::DatagramSocketRequest::Connect2 { addr, responder } => {
                            responder_send!(
                                responder,
                                &mut self.make_handler().await.connect(addr)
                            );
                        }
                        psocket::DatagramSocketRequest::Disconnect { responder } => {
                            responder_send!(responder, &mut Err(Errno::Eafnosupport));
                        }
                        psocket::DatagramSocketRequest::Clone { flags, object, .. } => {
                            let cloned_worker = self.clone().await;
                            self.clone_spawn(flags, object, cloned_worker);
                        }
                        psocket::DatagramSocketRequest::Close { responder } => {
                            let () = self.make_handler().await.close();
                            responder_send!(responder, zx::Status::OK.into_raw());
                            return Ok(());
                        }
                        psocket::DatagramSocketRequest::Sync { responder } => {
                            responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw());
                        }
                        psocket::DatagramSocketRequest::GetAttr { responder } => {
                            responder_send!(
                                responder,
                                zx::Status::NOT_SUPPORTED.into_raw(),
                                &mut fidl_fuchsia_io::NodeAttributes {
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
                        psocket::DatagramSocketRequest::SetAttr {
                            flags: _,
                            attributes: _,
                            responder,
                        } => {
                            responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw());
                        }
                        psocket::DatagramSocketRequest::Bind { addr: _, responder } => {
                            // NB: Netstack3 is not load-bearing enough to justify
                            // maintaining two implementations.
                            responder
                                .control_handle()
                                .shutdown_with_epitaph(zx::Status::NOT_SUPPORTED);
                        }
                        psocket::DatagramSocketRequest::Bind2 { addr, responder } => {
                            responder_send!(responder, &mut self.make_handler().await.bind(addr));
                        }
                        psocket::DatagramSocketRequest::GetSockName { responder } => {
                            // NB: Netstack3 is not load-bearing enough to justify
                            // maintaining two implementations.
                            responder
                                .control_handle()
                                .shutdown_with_epitaph(zx::Status::NOT_SUPPORTED);
                        }
                        psocket::DatagramSocketRequest::GetSockName2 { responder } => {
                            responder_send!(
                                responder,
                                &mut self.make_handler().await.get_sock_name()
                            );
                        }
                        psocket::DatagramSocketRequest::GetPeerName { responder } => {
                            // NB: Netstack3 is not load-bearing enough to justify
                            // maintaining two implementations.
                            responder
                                .control_handle()
                                .shutdown_with_epitaph(zx::Status::NOT_SUPPORTED);
                        }
                        psocket::DatagramSocketRequest::GetPeerName2 { responder } => {
                            responder_send!(
                                responder,
                                &mut self.make_handler().await.get_peer_name()
                            );
                        }
                        psocket::DatagramSocketRequest::SetSockOpt {
                            level,
                            optname,
                            optval: _,
                            responder,
                        } => {
                            warn!("UDP setsockopt {} {} not implemented", level, optname);
                            responder_send!(responder, &mut Err(Errno::Enoprotoopt))
                        }
                        psocket::DatagramSocketRequest::GetSockOpt {
                            level,
                            optname,
                            responder,
                        } => {
                            warn!("UDP getsockopt {} {} not implemented", level, optname);
                            responder_send!(responder, &mut Err(Errno::Enoprotoopt))
                        }
                        DatagramSocketRequest::Shutdown { how: _, responder } => {
                            // NB: Netstack3 is not load-bearing enough to justify
                            // maintaining two implementations.
                            responder
                                .control_handle()
                                .shutdown_with_epitaph(zx::Status::NOT_SUPPORTED);
                        }
                        DatagramSocketRequest::Shutdown2 { mode, responder } => responder_send!(
                            responder,
                            &mut self.make_handler().await.shutdown(mode)
                        ),
                        DatagramSocketRequest::RecvMsg {
                            addr_len: _,
                            data_len: _,
                            control_len: _,
                            flags: _,
                            responder,
                        } => {
                            // NB: Netstack3 is not load-bearing enough to justify
                            // maintaining two implementations.
                            responder
                                .control_handle()
                                .shutdown_with_epitaph(zx::Status::NOT_SUPPORTED);
                        }
                        DatagramSocketRequest::RecvMsg2 {
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
                        DatagramSocketRequest::SendMsg {
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
                        DatagramSocketRequest::SendMsg2 {
                            addr: _,
                            data: _,
                            control: _,
                            flags: _,
                            responder,
                        } => {
                            // NB: Netstack3 is not load-bearing enough to justify
                            // maintaining two implementations.
                            responder
                                .control_handle()
                                .shutdown_with_epitaph(zx::Status::NOT_SUPPORTED);
                        }
                        DatagramSocketRequest::NodeGetFlags { responder } => {
                            responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw(), 0);
                        }
                        DatagramSocketRequest::NodeSetFlags { flags: _, responder } => {
                            responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw());
                        }
                    }
                }
                Err(err) => {
                    let () = self.make_handler().await.close();
                    return Err(err);
                }
            }
        }
        // The loop breaks as the client side of the channel has been dropped, need
        // to treat that as an implicit close request as well.
        let () = self.make_handler().await.close();
        Ok(())
    }
}

impl<I: UdpSocketIpExt> BindingData<I> {
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
            info: SocketControlInfo { properties, state: SocketState::Unbound },
            available_data: VecDeque::new(),
            ref_count: 1,
        }
    }
}

struct RequestHandler<'a, I: UdpSocketIpExt, C: StackContext> {
    ctx: LockedStackContext<'a, C>,
    binding_id: usize,
    rights: u32,
    _marker: PhantomData<I>,
}

impl<'a, I, C> RequestHandler<'a, I, C>
where
    I: UdpSocketIpExt,
    C: UdpStackContext<I>,
    C::Dispatcher: InnerValue<UdpSocketCollection> + UdpEventDispatcher<I>,
{
    fn describe(&self) -> Option<fidl_fuchsia_io::NodeInfo> {
        self.get_state()
            .peer_event
            .duplicate_handle(zx::Rights::BASIC)
            .map(|peer| {
                fidl_fuchsia_io::NodeInfo::DatagramSocket(fidl_fuchsia_io::DatagramSocket {
                    event: peer,
                })
            })
            .ok()
    }

    fn get_state(&self) -> &BindingData<I> {
        I::get_collection(self.ctx.dispatcher()).binding_data.get(self.binding_id).unwrap()
    }

    fn get_state_mut(&mut self) -> &mut BindingData<I> {
        I::get_collection_mut(self.ctx.dispatcher_mut())
            .binding_data
            .get_mut(self.binding_id)
            .unwrap()
    }

    /// Handles a [POSIX socket connect request].
    ///
    /// [POSIX socket connect request]: psocket::DatagramSocketRequest::Connect
    fn connect(mut self, addr: fnet::SocketAddress) -> Result<(), Errno> {
        let sockaddr = I::SocketAddress::from_sock_addr(addr)?;
        trace!("connect UDP sockaddr: {:?}", sockaddr);
        let remote_port = sockaddr.get_specified_port().ok_or(Errno::Econnrefused)?;
        let remote_addr = sockaddr.get_specified_addr().ok_or(Errno::Einval)?;

        let (local_addr, local_port) = match self.get_state().info.state {
            SocketState::Unbound => {
                // do nothing, we're already unbound.
                // return None for local_addr and local_port.
                (None, None)
            }
            SocketState::BoundListen { listener_id } => {
                // if we're bound to a listen mode, we need to remove the
                // listener, and retrieve the bound local addr and port.
                let list_info = remove_udp_listener(self.ctx.deref_mut(), listener_id);
                // also remove from the EventLoop context:
                I::get_collection_mut(self.ctx.dispatcher_mut())
                    .listeners
                    .remove(&listener_id)
                    .unwrap();

                (list_info.local_ip, Some(list_info.local_port))
            }
            SocketState::BoundConnect { conn_id, .. } => {
                // if we're bound to a connect mode, we need to remove the
                // connection, and retrieve the bound local addr and port.
                let conn_info = remove_udp_conn(self.ctx.deref_mut(), conn_id);
                // also remove from the EventLoop context:
                I::get_collection_mut(self.ctx.dispatcher_mut()).conns.remove(&conn_id).unwrap();
                (Some(conn_info.local_ip), Some(conn_info.local_port))
            }
        };

        let conn_id =
            connect_udp(self.ctx.deref_mut(), local_addr, local_port, remote_addr, remote_port)
                .map_err(IntoErrno::into_errno)?;

        self.get_state_mut().info.state =
            SocketState::BoundConnect { conn_id, shutdown_read: false, shutdown_write: false };
        I::get_collection_mut(self.ctx.dispatcher_mut()).conns.insert(&conn_id, self.binding_id);
        Ok(())
    }

    /// Handles a [POSIX socket bind request].
    ///
    /// [POSIX socket bind request]: psocket::DatagramSocketRequest::Bind
    fn bind(mut self, addr: fnet::SocketAddress) -> Result<(), Errno> {
        let sockaddr = I::SocketAddress::from_sock_addr(addr)?;
        trace!("bind UDP sockaddr: {:?}", sockaddr);
        if self.get_state().info.state.is_bound() {
            return Err(Errno::Ealready);
        }
        let local_addr = sockaddr.get_specified_addr();
        let local_port = sockaddr.get_specified_port();

        let listener_id = listen_udp(self.ctx.deref_mut(), local_addr, local_port)
            .map_err(IntoErrno::into_errno)?;
        self.get_state_mut().info.state = SocketState::BoundListen { listener_id };
        I::get_collection_mut(self.ctx.dispatcher_mut())
            .listeners
            .insert(&listener_id, self.binding_id);
        Ok(())
    }

    /// Handles a [POSIX socket get_sock_name request].
    ///
    /// [POSIX socket get_sock_name request]: psocket::DatagramSocketRequest::GetSockName
    fn get_sock_name(self) -> Result<fnet::SocketAddress, Errno> {
        match self.get_state().info.state {
            SocketState::Unbound { .. } => {
                return Err(Errno::Enotsock);
            }
            SocketState::BoundConnect { conn_id, .. } => {
                let info = get_udp_conn_info(self.ctx.deref(), conn_id);
                Ok(I::SocketAddress::new(*info.local_ip, info.local_port.get()).into_sock_addr())
            }
            SocketState::BoundListen { listener_id } => {
                let info = get_udp_listener_info(self.ctx.deref(), listener_id);
                let local_ip = match info.local_ip {
                    Some(addr) => *addr,
                    None => I::UNSPECIFIED_ADDRESS,
                };
                Ok(I::SocketAddress::new(local_ip, info.local_port.get()).into_sock_addr())
            }
        }
    }

    /// Handles a [POSIX socket get_peer_name request].
    ///
    /// [POSIX socket get_peer_name request]: psocket::DatagramSocketRequest::GetPeerName
    fn get_peer_name(self) -> Result<fnet::SocketAddress, Errno> {
        match self.get_state().info.state {
            SocketState::Unbound { .. } => {
                return Err(Errno::Enotsock);
            }
            SocketState::BoundListen { .. } => {
                return Err(Errno::Enotconn);
            }
            SocketState::BoundConnect { conn_id, .. } => {
                let info = get_udp_conn_info(self.ctx.deref(), conn_id);
                Ok(I::SocketAddress::new(*info.remote_ip, info.remote_port.get()).into_sock_addr())
            }
        }
    }

    fn close_core(&mut self) {
        match self.get_state().info.state {
            SocketState::Unbound => {} // nothing to do
            SocketState::BoundListen { listener_id } => {
                // remove from bindings:
                I::get_collection_mut(self.ctx.dispatcher_mut()).listeners.remove(&listener_id);
                // remove from core:
                remove_udp_listener(self.ctx.deref_mut(), listener_id);
            }
            SocketState::BoundConnect { conn_id, .. } => {
                // remove from bindings:
                I::get_collection_mut(self.ctx.dispatcher_mut()).conns.remove(&conn_id);
                // remove from core:
                remove_udp_conn(self.ctx.deref_mut(), conn_id);
            }
        }
        self.get_state_mut().info.state = SocketState::Unbound;
    }

    fn close(mut self) {
        let inner = self.get_state_mut();
        if inner.ref_count == 1 {
            // always make sure the socket is closed with core.
            self.close_core();
            I::get_collection_mut(self.ctx.dispatcher_mut())
                .binding_data
                .remove(self.binding_id)
                .unwrap();
        } else {
            inner.ref_count -= 1;
        }
    }

    fn recv_msg(
        &mut self,
        want_addr: bool,
        data_len: usize,
    ) -> Result<
        (Option<Box<fidl_fuchsia_net::SocketAddress>>, Vec<u8>, psocket::RecvControlData, u32),
        Errno,
    > {
        let () = self.need_rights(fio::OPEN_RIGHT_READABLE)?;
        let state = self.get_state_mut();
        let available = if let Some(front) = state.available_data.pop_front() {
            front
        } else {
            if let SocketState::BoundConnect { shutdown_read, .. } = state.info.state {
                if shutdown_read {
                    // Return empty data to signal EOF.
                    return Ok((None, Vec::new(), psocket::RecvControlData::empty(), 0));
                }
            }
            return Err(Errno::Eagain);
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
                error!("UDP socket failed to signal peer: {:?}", e);
            }
        }
        Ok((
            addr,
            data,
            psocket::RecvControlData::empty(),
            truncated.try_into().unwrap_or(std::u32::MAX),
        ))
    }

    fn send_msg(&mut self, addr: Option<fnet::SocketAddress>, data: Vec<u8>) -> Result<i64, Errno> {
        let () = self.need_rights(fio::OPEN_RIGHT_WRITABLE)?;
        let remote = if let Some(addr) = addr {
            let addr = I::SocketAddress::from_sock_addr(addr)?;
            Some(
                addr.get_specified_addr()
                    .and_then(|a| addr.get_specified_port().map(|p| (a, p)))
                    .ok_or(Errno::Einval)?,
            )
        } else {
            None
        };
        let len = data.len() as i64;
        let body = Buf::new(data, ..);
        match self.get_state().info.state {
            SocketState::Unbound => {
                // TODO(brunodalbo) if destination address is set, we should
                // auto-bind here (check POSIX compliance).
                Err(Errno::Edestaddrreq)
            }
            SocketState::BoundConnect { conn_id, shutdown_write, .. } => {
                if shutdown_write {
                    return Err(Errno::Epipe);
                }
                match remote {
                    Some((addr, port)) => {
                        // Caller specified a remote socket address; use stateless
                        // UDP send using the local address and port in `conn_id`.
                        let conn_info = get_udp_conn_info(self.ctx.deref(), conn_id);
                        send_udp::<I, Buf<Vec<u8>>, _>(
                            self.ctx.deref_mut(),
                            Some(conn_info.local_ip),
                            Some(conn_info.local_port),
                            addr,
                            port,
                            body,
                        )
                        .map_err(IntoErrno::into_errno)
                    }
                    None => {
                        // Caller did not specify a remote socket address; just use
                        // the existing conn.
                        send_udp_conn::<_, Buf<Vec<u8>>, _>(self.ctx.deref_mut(), conn_id, body)
                            .map_err(IntoErrno::into_errno)
                    }
                }
            }
            SocketState::BoundListen { listener_id } => match remote {
                Some((addr, port)) => send_udp_listener::<_, Buf<Vec<u8>>, _>(
                    self.ctx.deref_mut(),
                    listener_id,
                    None,
                    addr,
                    port,
                    body,
                )
                .map_err(IntoErrno::into_errno),
                None => Err(Errno::Edestaddrreq),
            },
        }
        .map(|()| len)
    }

    fn shutdown(mut self, how: psocket::ShutdownMode) -> Result<(), Errno> {
        // Only "connected" UDP sockets can be shutdown.
        if let SocketState::BoundConnect { ref mut shutdown_read, ref mut shutdown_write, .. } =
            self.get_state_mut().info.state
        {
            if how.is_empty() {
                return Err(Errno::Einval);
            }
            // Shutting down a socket twice is valid so we can just blindly
            // set the corresponding flags.
            if how.contains(psocket::ShutdownMode::Write) {
                *shutdown_write = true;
            }
            if how.contains(psocket::ShutdownMode::Read) {
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
        Err(Errno::Enotconn)
    }

    /// Tests if we have sufficient rights as required; if we don't, then
    /// an error is returned.
    fn need_rights(&self, required: u32) -> Result<(), Errno> {
        if self.rights & required == 0 {
            return Err(Errno::Eperm);
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::{endpoints::ServerEnd, AsyncChannel};
    use fidl_fuchsia_io as fio;
    use fuchsia_async as fasync;
    use fuchsia_zircon::{self as zx, AsHandleRef};
    use futures::StreamExt;

    use crate::bindings::integration_tests::{
        test_ep_name, StackSetupBuilder, TestSetup, TestSetupBuilder, TestStack,
    };
    use crate::bindings::socket::testutil::TestSockAddr;
    use net_types::ip::{Ip, IpAddress};

    async fn udp_prepare_test<A: TestSockAddr>() -> (TestSetup, psocket::DatagramSocketProxy) {
        let mut t = TestSetupBuilder::new()
            .add_endpoint()
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint(test_ep_name(1), Some(A::config_addr_subnet())),
            )
            .build()
            .await
            .unwrap();
        let proxy = get_socket::<A>(t.get(0)).await;
        (t, proxy)
    }

    async fn get_socket<A: TestSockAddr>(
        test_stack: &mut TestStack,
    ) -> psocket::DatagramSocketProxy {
        let socket_provider = test_stack.connect_socket_provider().unwrap();
        let socket = socket_provider
            .datagram_socket(A::DOMAIN, psocket::DatagramSocketProtocol::Udp)
            .await
            .unwrap()
            .expect("Socket succeeds");
        psocket::DatagramSocketProxy::new(
            fasync::Channel::from_channel(socket.into_channel()).unwrap(),
        )
    }

    async fn get_socket_and_event<A: TestSockAddr>(
        test_stack: &mut TestStack,
    ) -> (psocket::DatagramSocketProxy, zx::EventPair) {
        let ctlr = get_socket::<A>(test_stack).await;
        let node_info = ctlr.describe().await.expect("Socked describe succeeds");
        let event = match node_info {
            fidl_fuchsia_io::NodeInfo::DatagramSocket(e) => e.event,
            _ => panic!("Got wrong describe response for UDP socket"),
        };
        (ctlr, event)
    }

    async fn test_udp_connect_failure<A: TestSockAddr>() {
        let (_t, proxy) = udp_prepare_test::<A>().await;

        // Pass a bad domain.
        let res = proxy
            .connect2(&mut A::DifferentDomain::create(A::DifferentDomain::LOCAL_ADDR, 1010))
            .await
            .unwrap()
            .expect_err("connect fails");
        assert_eq!(res, Errno::Eafnosupport);

        // Pass an unspecified remote address.
        let res = proxy
            .connect2(&mut A::create(
                <A::AddrType as IpAddress>::Version::UNSPECIFIED_ADDRESS,
                1010,
            ))
            .await
            .unwrap()
            .expect_err("connect fails");
        assert_eq!(res, Errno::Einval);

        // Pass a bad port.
        let res = proxy
            .connect2(&mut A::create(A::LOCAL_ADDR, 0))
            .await
            .unwrap()
            .expect_err("connect fails");
        assert_eq!(res, Errno::Econnrefused);

        // Pass an unreachable address (tests error forwarding from `udp_connect`).
        let res = proxy
            .connect2(&mut A::create(A::UNREACHABLE_ADDR, 1010))
            .await
            .unwrap()
            .expect_err("connect fails");
        assert_eq!(res, Errno::Enetunreach);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_connect_failure_v4() {
        test_udp_connect_failure::<fnet::Ipv4SocketAddress>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_connect_failure_v6() {
        test_udp_connect_failure::<fnet::Ipv6SocketAddress>().await;
    }

    async fn test_udp_connect<A: TestSockAddr>() {
        let (_t, proxy) = udp_prepare_test::<A>().await;
        let () = proxy
            .connect2(&mut A::create(A::REMOTE_ADDR, 200))
            .await
            .unwrap()
            .expect("connect succeeds");

        // Can connect again to a different remote should succeed.
        let () = proxy
            .connect2(&mut A::create(A::REMOTE_ADDR_2, 200))
            .await
            .unwrap()
            .expect("connect suceeds");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_connect_v4() {
        test_udp_connect::<fnet::Ipv4SocketAddress>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_connect_v6() {
        test_udp_connect::<fnet::Ipv6SocketAddress>().await;
    }

    async fn test_udp_bind<A: TestSockAddr>() {
        let (mut t, socket) = udp_prepare_test::<A>().await;
        let stack = t.get(0);
        // Can bind to local address.
        let () =
            socket.bind2(&mut A::create(A::LOCAL_ADDR, 200)).await.unwrap().expect("bind succeeds");

        // Can't bind again (to another port).
        let res = socket
            .bind2(&mut A::create(A::LOCAL_ADDR, 201))
            .await
            .unwrap()
            .expect_err("bind fails");
        assert_eq!(res, Errno::Ealready);

        // Can bind another socket to a different port.
        let socket = get_socket::<A>(stack).await;
        let () =
            socket.bind2(&mut A::create(A::LOCAL_ADDR, 201)).await.unwrap().expect("bind succeeds");

        // Can bind to unspecified address in a different port.
        let socket = get_socket::<A>(stack).await;
        let () = socket
            .bind2(&mut A::create(<A::AddrType as IpAddress>::Version::UNSPECIFIED_ADDRESS, 202))
            .await
            .unwrap()
            .expect("bind succeeds");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_bind_v4() {
        test_udp_bind::<fnet::Ipv4SocketAddress>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_bind_v6() {
        test_udp_bind::<fnet::Ipv6SocketAddress>().await;
    }

    async fn test_udp_bind_then_connect<A: TestSockAddr>() {
        let (_t, socket) = udp_prepare_test::<A>().await;
        // Can bind to local address.
        let () =
            socket.bind2(&mut A::create(A::LOCAL_ADDR, 200)).await.unwrap().expect("bind suceeds");

        let () = socket
            .connect2(&mut A::create(A::REMOTE_ADDR, 1010))
            .await
            .unwrap()
            .expect("connect succeeds");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_bind_then_connect_v4() {
        test_udp_bind_then_connect::<fnet::Ipv4SocketAddress>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_bind_then_connect_v6() {
        test_udp_bind_then_connect::<fnet::Ipv6SocketAddress>().await;
    }

    /// Tests a simple UDP setup with a client and a server, where the client
    /// can send data to the server and the server receives it.
    async fn test_udp_hello<A: TestSockAddr>() {
        // We create two stacks, Alice (server listening on LOCAL_ADDR:200), and
        // Bob (client, bound on REMOTE_ADDR:300).
        // After setup, Bob connects to Alice and sends a datagram.
        // Finally, we verify that Alice receives the datagram.
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
        let (alice_socket, alice_events) = get_socket_and_event::<A>(alice).await;

        // Verify that Alice has no local or peer addresses bound
        assert_eq!(
            alice_socket.get_sock_name2().await.unwrap().expect_err("alice getsockname fails"),
            Errno::Enotsock
        );
        assert_eq!(
            alice_socket.get_peer_name2().await.unwrap().expect_err("alice getpeername fails"),
            Errno::Enotsock
        );

        // Setup Alice as a server, bound to LOCAL_ADDR:200
        println!("Configuring alice...");
        let () = alice_socket
            .bind2(&mut A::create(A::LOCAL_ADDR, 200))
            .await
            .unwrap()
            .expect("alice bind suceeds");

        // Verify that Alice is listening on the local socket, but still has no peer socket
        assert_eq!(
            alice_socket.get_sock_name2().await.unwrap().expect("alice getsockname succeeds"),
            A::create(A::LOCAL_ADDR, 200)
        );
        assert_eq!(
            alice_socket
                .get_peer_name2()
                .await
                .unwrap()
                .expect_err("alice getpeername should fail"),
            Errno::Enotconn
        );

        // check that alice has no data to read, and it'd block waiting for
        // events:
        assert_eq!(
            alice_socket
                .recv_msg2(false, 2048, false, psocket::RecvMsgFlags::empty())
                .await
                .unwrap()
                .expect_err("Reading from alice should fail"),
            Errno::Eagain
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
        let (bob_socket, bob_events) = get_socket_and_event::<A>(bob).await;
        let () = bob_socket
            .bind2(&mut A::create(A::REMOTE_ADDR, 300))
            .await
            .unwrap()
            .expect("bob bind suceeds");

        // Verify that Bob is listening on the local socket, but has no peer socket
        assert_eq!(
            bob_socket.get_sock_name2().await.unwrap().expect("bob getsockname suceeds"),
            A::create(A::REMOTE_ADDR, 300)
        );
        assert_eq!(
            bob_socket
                .get_peer_name2()
                .await
                .unwrap()
                .expect_err("get peer name should fail before connected"),
            Errno::Enotconn
        );

        // Connect Bob to Alice on LOCAL_ADDR:200
        println!("Connecting bob to alice...");
        let () = bob_socket
            .connect2(&mut A::create(A::LOCAL_ADDR, 200))
            .await
            .unwrap()
            .expect("Connect succeeds");

        // Verify that Bob has the peer socket set correctly
        assert_eq!(
            bob_socket.get_peer_name2().await.unwrap().expect("bob getpeername suceeds"),
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
                    psocket::SendControlData::empty(),
                    psocket::SendMsgFlags::empty()
                )
                .await
                .unwrap()
                .expect("sendmsg suceeds"),
            body.len() as i64
        );

        // Wait for datagram to arrive on Alice's socket:

        println!("Waiting for signals");
        fasync::OnSignals::new(&alice_events, ZXSIO_SIGNAL_INCOMING)
            .await
            .expect("waiting for readable succeeds");

        let (from, data, _, truncated) = alice_socket
            .recv_msg2(true, 2048, false, psocket::RecvMsgFlags::empty())
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

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_hello_v4() {
        test_udp_hello::<fnet::Ipv4SocketAddress>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_hello_v6() {
        test_udp_hello::<fnet::Ipv6SocketAddress>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_socket_describe() {
        let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
        let test_stack = t.get(0);
        let socket_provider = test_stack.connect_socket_provider().unwrap();
        let socket = socket_provider
            .datagram_socket(psocket::Domain::Ipv4, psocket::DatagramSocketProtocol::Udp)
            .await
            .unwrap()
            .expect("Socket call succeeds")
            .into_proxy()
            .unwrap();
        let info = socket.describe().await.expect("Describe call succeeds");
        match info {
            fidl_fuchsia_io::NodeInfo::DatagramSocket(_) => (),
            info => panic!(
                "Socket Describe call did not return Node of type Socket, got {:?} instead",
                info
            ),
        }
    }

    async fn socket_clone(
        socket: &psocket::DatagramSocketProxy,
        flags: u32,
    ) -> Result<psocket::DatagramSocketProxy, Error> {
        let (server, client) = zx::Channel::create()?;
        socket.clone(flags, ServerEnd::from(server))?;
        let channel = AsyncChannel::from_channel(client)?;
        Ok(psocket::DatagramSocketProxy::new(channel))
    }

    async fn test_udp_clone<A: TestSockAddr>()
    where
        <A::AddrType as IpAddress>::Version: UdpSocketIpExt,
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
        let (alice_socket, alice_events) = get_socket_and_event::<A>(t.get(0)).await;
        // Test for the OPEN_FLAG_DESCRIBE.
        let alice_cloned =
            socket_clone(&alice_socket, fio::CLONE_FLAG_SAME_RIGHTS | fio::OPEN_FLAG_DESCRIBE)
                .await
                .expect("cannot clone socket");
        let mut events = alice_cloned.take_event_stream();
        let psocket::DatagramSocketEvent::OnOpen_ { s, info } =
            events.next().await.expect("stream closed").expect("failed to decode");
        assert_eq!(s, zx::sys::ZX_OK);
        let info = info.unwrap();
        match *info {
            fidl_fuchsia_io::NodeInfo::DatagramSocket(_) => (),
            info => panic!(
                "Socket Describe call did not return Node of type Socket, got {:?} instead",
                info
            ),
        }
        // describe() explicitly.
        let info = alice_cloned.describe().await.expect("Describe call succeeds");
        match info {
            fidl_fuchsia_io::NodeInfo::DatagramSocket(_) => (),
            info => panic!(
                "Socket Describe call did not return Node of type Socket, got {:?} instead",
                info
            ),
        }

        alice_socket
            .bind2(&mut A::create(A::LOCAL_ADDR, 200))
            .await
            .unwrap()
            .expect("failed to bind for alice");
        // We should be able to read that back from the cloned socket.
        assert_eq!(
            alice_cloned.get_sock_name2().await.unwrap().expect("failed to getsockname for alice"),
            A::create(A::LOCAL_ADDR, 200)
        );

        let (bob_socket, bob_events) = get_socket_and_event::<A>(t.get(1)).await;
        let bob_cloned = socket_clone(&bob_socket, fio::CLONE_FLAG_SAME_RIGHTS)
            .await
            .expect("failed to clone socket");
        bob_cloned
            .bind2(&mut A::create(A::REMOTE_ADDR, 200))
            .await
            .unwrap()
            .expect("failed to bind for bob");
        // We should be able to read that back from the original socket.
        assert_eq!(
            bob_socket.get_sock_name2().await.unwrap().expect("failed to getsockname for bob"),
            A::create(A::REMOTE_ADDR, 200)
        );

        let body = "Hello".as_bytes();
        assert_eq!(
            alice_socket
                .send_msg(
                    Some(&mut A::create(A::REMOTE_ADDR, 200)),
                    &body,
                    psocket::SendControlData::empty(),
                    psocket::SendMsgFlags::empty()
                )
                .await
                .unwrap()
                .expect("failed to send_msg"),
            body.len() as i64
        );

        fasync::OnSignals::new(&bob_events, ZXSIO_SIGNAL_INCOMING)
            .await
            .expect("failed to wait for readable event on bob");

        // Receive from the cloned socket.
        let (from, data, _, truncated) = bob_cloned
            .recv_msg2(true, 2048, false, psocket::RecvMsgFlags::empty())
            .await
            .unwrap()
            .expect("failed to recv_msg");
        assert_eq!(&data[..], body);
        assert_eq!(truncated, 0);
        assert_eq!(from.map(|a| *a), Some(A::create(A::LOCAL_ADDR, 200)));
        // The data have already been received on the cloned socket
        assert_eq!(
            bob_socket
                .recv_msg2(false, 2048, false, psocket::RecvMsgFlags::empty())
                .await
                .unwrap()
                .expect_err("Reading from bob should fail"),
            Errno::Eagain
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
                        psocket::SendControlData::empty(),
                        psocket::SendMsgFlags::empty()
                    )
                    .await
                    .unwrap()
                    .expect_err("should not send_msg on a readonly socket"),
                Errno::Eperm,
            );

            assert_eq!(
                bob_writeonly
                    .recv_msg2(false, 2048, false, psocket::RecvMsgFlags::empty())
                    .await
                    .unwrap()
                    .expect_err("should not recv_msg on a writeonly socket"),
                Errno::Eperm,
            );

            assert_eq!(
                bob_writeonly
                    .send_msg(
                        Some(&mut A::create(A::LOCAL_ADDR, 200)),
                        &body,
                        psocket::SendControlData::empty(),
                        psocket::SendMsgFlags::empty()
                    )
                    .await
                    .unwrap()
                    .expect("failed to send_msg on bob writeonly"),
                body.len() as i64
            );

            let alice_readonly_info = alice_readonly.describe().await.expect("failed to describe");
            let alice_readonly_event = match alice_readonly_info {
                fidl_fuchsia_io::NodeInfo::DatagramSocket(e) => e.event,
                _ => panic!("Got wrong describe response for UDP socket"),
            };
            fasync::OnSignals::new(&alice_readonly_event, ZXSIO_SIGNAL_INCOMING)
                .await
                .expect("failed to wait for readable event on alice readonly");

            let (from, data, _, truncated) = alice_readonly
                .recv_msg2(true, 2048, false, psocket::RecvMsgFlags::empty())
                .await
                .unwrap()
                .expect("failed to recv_msg on alice readonly");
            assert_eq!(&data[..], body);
            assert_eq!(truncated, 0);
            assert_eq!(from.map(|a| *a), Some(A::create(A::REMOTE_ADDR, 200)));
        }

        // Close the socket should not invalidate the cloned socket.
        bob_socket.close().await.expect("failed to close bob's socket");
        assert_eq!(
            bob_cloned
                .send_msg(
                    Some(&mut A::create(A::LOCAL_ADDR, 200)),
                    &body,
                    psocket::SendControlData::empty(),
                    psocket::SendMsgFlags::empty()
                )
                .await
                .unwrap()
                .expect("failed to send_msg"),
            body.len() as i64
        );

        alice_cloned.close().await.expect("failed to close");
        fasync::OnSignals::new(&alice_events, ZXSIO_SIGNAL_INCOMING)
            .await
            .expect("failed to wait for readable event on alice");

        let (from, data, _, truncated) = alice_socket
            .recv_msg2(true, 2048, false, psocket::RecvMsgFlags::empty())
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
                    let disp: &BindingsDispatcher = ctx.dispatcher().inner();
                    let udpsocks: &UdpSocketCollectionInner<<A::AddrType as IpAddress>::Version> =
                        UdpSocketIpExt::get_collection(disp);
                    assert_eq!(udpsocks.binding_data.iter().count(), 1);
                    assert_eq!(udpsocks.listeners.iter().count(), 1);
                    assert!(udpsocks.conns.is_empty());
                })
                .await;
        }

        alice_socket.close().await.expect("failed to close");
        bob_cloned.close().await.expect("failed to close");

        // But the sockets should have gone here.
        for i in 0..2 {
            t.get(i)
                .with_ctx(|ctx| {
                    let disp: &BindingsDispatcher = ctx.dispatcher().inner();
                    let udpsocks: &UdpSocketCollectionInner<<A::AddrType as IpAddress>::Version> =
                        UdpSocketIpExt::get_collection(disp);
                    assert!(udpsocks.binding_data.is_empty());
                    assert!(udpsocks.listeners.is_empty());
                    assert!(udpsocks.conns.is_empty());
                })
                .await;
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_clone_v4() {
        test_udp_clone::<fnet::Ipv4SocketAddress>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_clone_v6() {
        test_udp_clone::<fnet::Ipv6SocketAddress>().await;
    }

    async fn test_close_twice<A: TestSockAddr>()
    where
        <A::AddrType as IpAddress>::Version: UdpSocketIpExt,
    {
        // Make sure we cannot close twice from the same channel so that
        // we maintain the correct refcount.
        let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
        let test_stack = t.get(0);
        let socket = get_socket::<A>(test_stack).await;
        let cloned = socket_clone(&socket, fio::CLONE_FLAG_SAME_RIGHTS).await.unwrap();
        socket.close().await.expect("failed to close the socket");
        socket
            .close()
            .await
            .expect_err("should not be able to close the socket twice on the same channel");
        assert!(socket.into_channel().unwrap().is_closed());
        // Since we still hold the cloned socket, the binding_data shouldn't be empty
        test_stack
            .with_ctx(|ctx| {
                let disp: &BindingsDispatcher = ctx.dispatcher().inner();
                let udpsocks: &UdpSocketCollectionInner<<A::AddrType as IpAddress>::Version> =
                    UdpSocketIpExt::get_collection(disp);
                assert!(!udpsocks.binding_data.is_empty());
            })
            .await;
        cloned.close().await.expect("failed to close the socket");
        // Now it should become empty
        test_stack
            .with_ctx(|ctx| {
                let disp: &BindingsDispatcher = ctx.dispatcher().inner();
                let udpsocks: &UdpSocketCollectionInner<<A::AddrType as IpAddress>::Version> =
                    UdpSocketIpExt::get_collection(disp);
                assert!(udpsocks.binding_data.is_empty());
            })
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_close_twice_v4() {
        test_close_twice::<fnet::Ipv4SocketAddress>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_close_twice_v6() {
        test_close_twice::<fnet::Ipv6SocketAddress>().await;
    }

    async fn test_implicit_close<A: TestSockAddr>()
    where
        <A::AddrType as IpAddress>::Version: UdpSocketIpExt,
    {
        let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
        let test_stack = t.get(0);
        let cloned = {
            let socket = get_socket::<A>(test_stack).await;
            socket_clone(&socket, fio::CLONE_FLAG_SAME_RIGHTS).await.unwrap()
            // socket goes out of scope indicating an implicit close.
        };
        // Using an explicit close here.
        cloned.close().await.expect("failed to close");
        // No socket should be there now.
        test_stack
            .with_ctx(|ctx| {
                let disp: &BindingsDispatcher = ctx.dispatcher().inner();
                let udpsocks: &UdpSocketCollectionInner<<A::AddrType as IpAddress>::Version> =
                    UdpSocketIpExt::get_collection(disp);
                assert!(udpsocks.binding_data.is_empty());
            })
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_implicit_close_v4() {
        test_implicit_close::<fnet::Ipv4SocketAddress>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_implicit_close_v6() {
        test_implicit_close::<fnet::Ipv6SocketAddress>().await;
    }

    async fn expect_clone_invalid_args(socket: &psocket::DatagramSocketProxy, flags: u32) {
        let cloned = socket_clone(&socket, flags).await.unwrap();
        {
            let mut events = cloned.take_event_stream();
            let psocket::DatagramSocketEvent::OnOpen_ { s, .. } =
                events.next().await.expect("stream closed").expect("failed to decode");
            assert_eq!(s, zx::sys::ZX_ERR_INVALID_ARGS);
        }
        assert!(cloned.into_channel().unwrap().is_closed());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_clone_args() {
        let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
        let test_stack = t.get(0);
        let socket = get_socket::<fnet::Ipv4SocketAddress>(test_stack).await;
        // conflicting flags
        expect_clone_invalid_args(&socket, fio::CLONE_FLAG_SAME_RIGHTS | fio::OPEN_RIGHT_READABLE)
            .await;
        // no remote
        expect_clone_invalid_args(&socket, fio::OPEN_FLAG_NO_REMOTE).await;
        // append
        expect_clone_invalid_args(&socket, fio::OPEN_FLAG_APPEND).await;
        // admin
        expect_clone_invalid_args(&socket, fio::OPEN_RIGHT_ADMIN).await;
        // executable
        expect_clone_invalid_args(&socket, fio::OPEN_RIGHT_EXECUTABLE).await;
        socket.close().await.expect("failed to close");

        // make sure we don't leak anything.
        test_stack
            .with_ctx(|ctx| {
                let disp: &BindingsDispatcher = ctx.dispatcher().inner();
                let udpsocks: &UdpSocketCollectionInner<net_types::ip::Ipv4> =
                    UdpSocketIpExt::get_collection(disp);
                assert!(udpsocks.binding_data.is_empty());
            })
            .await;
    }

    async fn test_udp_shutdown<A: TestSockAddr>() {
        let mut t = TestSetupBuilder::new()
            .add_endpoint()
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint(test_ep_name(1), Some(A::config_addr_subnet())),
            )
            .build()
            .await
            .unwrap();
        let (socket, events) = get_socket_and_event::<A>(t.get(0)).await;
        let mut local = A::create(A::LOCAL_ADDR, 200);
        let mut remote = A::create(A::REMOTE_ADDR, 300);
        assert_eq!(
            socket
                .shutdown2(psocket::ShutdownMode::Write)
                .await
                .unwrap()
                .expect_err("should not shutdown an unconnected socket"),
            Errno::Enotconn,
        );
        socket.bind2(&mut local).await.unwrap().expect("failed to bind");
        assert_eq!(
            socket
                .shutdown2(psocket::ShutdownMode::Write)
                .await
                .unwrap()
                .expect_err("should not shutdown an unconnected socket"),
            Errno::Enotconn,
        );
        socket.connect2(&mut remote).await.unwrap().expect("failed to connect");
        assert_eq!(
            socket
                .shutdown2(psocket::ShutdownMode::empty())
                .await
                .unwrap()
                .expect_err("invalid args"),
            Errno::Einval
        );

        // Cannot send
        let body = "Hello".as_bytes();
        socket.shutdown2(psocket::ShutdownMode::Write).await.unwrap().expect("failed to shutdown");
        assert_eq!(
            socket
                .send_msg(
                    None,
                    &body,
                    psocket::SendControlData::empty(),
                    psocket::SendMsgFlags::empty()
                )
                .await
                .unwrap()
                .expect_err("writing to an already-shutdown socket should fail"),
            Errno::Epipe,
        );
        let mut invalid_addr = A::create(A::REMOTE_ADDR, 0);
        assert_eq!(
            socket.send_msg(Some(&mut invalid_addr), &body, psocket::SendControlData::empty(), psocket::SendMsgFlags::empty()).await.unwrap().expect_err(
                "writing to an invalid address (port 0) should fail with EINVAL instead of EPIPE"
            ),
            Errno::Einval,
        );

        let (e1, e2) = zx::EventPair::create().unwrap();
        fasync::Task::spawn(async move {
            fasync::OnSignals::new(&events, ZXSIO_SIGNAL_INCOMING)
                .await
                .expect("should become unblocked because of the shutdown");

            e1.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).unwrap();
        })
        .detach();

        socket.shutdown2(psocket::ShutdownMode::Read).await.unwrap().expect("failed to shutdown");
        let (_, data, _, _) = socket
            .recv_msg2(false, 2048, false, psocket::RecvMsgFlags::empty())
            .await
            .unwrap()
            .expect("recvmsg should return empty data");
        assert!(data.is_empty());

        fasync::OnSignals::new(&e2, zx::Signals::USER_0).await.expect("must be signaled");

        socket
            .shutdown2(psocket::ShutdownMode::Read)
            .await
            .unwrap()
            .expect("failed to shutdown the socket twice");
        socket
            .shutdown2(psocket::ShutdownMode::Write)
            .await
            .unwrap()
            .expect("failed to shutdown the socket twice");
        socket
            .shutdown2(psocket::ShutdownMode::Read | psocket::ShutdownMode::Write)
            .await
            .unwrap()
            .expect("failed to shutdown the socket twice");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_shutdown_v4() {
        test_udp_shutdown::<fnet::Ipv4SocketAddress>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_shutdown_v6() {
        test_udp_shutdown::<fnet::Ipv6SocketAddress>().await;
    }
}
