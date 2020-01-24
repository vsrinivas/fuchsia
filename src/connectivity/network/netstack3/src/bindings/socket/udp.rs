// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! UDP socket bindings.

use std::convert::TryInto;
use std::num::NonZeroU16;
use std::ops::{Deref, DerefMut};

use anyhow::{format_err, Error};
use fidl_fuchsia_posix_socket::{self as psocket, DatagramSocketRequest};
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, prelude::HandleBased, Peered};
use futures::{FutureExt, TryFutureExt, TryStreamExt};
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
    _marker: std::marker::PhantomData<I>,
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
    BoundConnect { conn_id: UdpConnId<I> },
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
) -> Result<(), libc::c_int>
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
    ) -> Result<(), libc::c_int> {
        let (local_event, peer_event) = zx::EventPair::create().map_err(|_| libc::ENOBUFS)?;
        // signal peer that OUTGOING is available.
        // TODO(brunodalbo): We're currently not enforcing any sort of
        // flow-control for outgoing UDP datagrams. That'll get fixed once we
        // limit the number of in flight datagrams per socket (i.e. application
        // buffers).
        if let Err(e) = local_event.signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_OUTGOING) {
            error!("UDP socket failed to signal peer: {:?}", e);
        }
        fasync::spawn(
            async move {
                let id = {
                    let mut locked = ctx.lock().await;
                    I::get_collection_mut(locked.dispatcher_mut())
                        .binding_data
                        .push(BindingData::<I>::new(local_event, peer_event, properties))
                };
                let worker = Self { ctx, id, _marker: std::marker::PhantomData };

                let result = events.try_for_each(|req| worker.handle_request(req).map(Ok)).await;

                let mut handler = worker.make_handler().await;
                let inner = handler.get_state_mut();
                if inner.ref_count == 1 {
                    // always make sure the socket is closed with core.
                    handler.close_core();
                    I::get_collection_mut(handler.ctx.dispatcher_mut())
                        .binding_data
                        .remove(worker.id)
                        .unwrap();
                } else {
                    inner.ref_count -= 1;
                }

                result
            }
            // When the closure above finishes, that means `self` goes out
            // of scope and is dropped, meaning that the event stream's
            // underlying channel is closed.
            // If any errors occured as a result of the closure, we just log
            // them.
            .unwrap_or_else(|e: fidl::Error| error!("UDP socket control request error: {:?}", e)),
        );
        Ok(())
    }

    async fn make_handler(&self) -> RequestHandler<'_, I, C> {
        let ctx = self.ctx.lock().await;
        RequestHandler { ctx, binding_id: self.id, _marker: std::marker::PhantomData }
    }

    /// Handles a [POSIX socket request].
    ///
    /// [POSIX socket request]: psocket::DatagramSocketRequest
    async fn handle_request(&self, req: psocket::DatagramSocketRequest) {
        match req {
            psocket::DatagramSocketRequest::Describe { responder } => {
                // If the call to duplicate_handle fails, we have no choice but to drop the
                // responder and close the channel, since Describe must be infallible.
                if let Some(mut info) = self.make_handler().await.describe() {
                    responder_send!(responder, &mut info);
                }
            }
            psocket::DatagramSocketRequest::Connect { addr, responder } => {
                responder_send!(responder, &mut self.make_handler().await.connect(addr));
            }
            psocket::DatagramSocketRequest::Clone { .. } => {
                warn!("UDP socket Clone not implemented!");
            }
            psocket::DatagramSocketRequest::Close { responder } => {
                responder_send!(responder, self.make_handler().await.close().err().unwrap_or(0i32));
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
            psocket::DatagramSocketRequest::SetAttr { flags: _, attributes: _, responder } => {
                responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw());
            }
            psocket::DatagramSocketRequest::Bind { addr, responder } => {
                responder_send!(responder, &mut self.make_handler().await.bind(addr));
            }
            psocket::DatagramSocketRequest::GetSockName { responder } => {
                responder_send!(responder, &mut self.make_handler().await.get_sock_name());
            }
            psocket::DatagramSocketRequest::GetPeerName { responder } => {
                responder_send!(responder, &mut self.make_handler().await.get_peer_name());
            }
            psocket::DatagramSocketRequest::SetSockOpt { level, optname, optval: _, responder } => {
                warn!("UDP setsockopt {} {} not implemented", level, optname);
                responder_send!(responder, &mut Err(libc::ENOPROTOOPT))
            }
            psocket::DatagramSocketRequest::GetSockOpt { level, optname, responder } => {
                warn!("UDP getsockopt {} {} not implemented", level, optname);
                responder_send!(responder, &mut Err(libc::ENOPROTOOPT))
            }

            DatagramSocketRequest::Shutdown { how: _, responder: _ } => {
                warn!("UDP Shutdown not implemented");
            }
            DatagramSocketRequest::RecvMsg {
                addr_len,
                data_len,
                control_len: _,
                flags: _,
                responder,
            } => {
                // TODO(brunodalbo) handle control and flags
                responder_send!(
                    responder,
                    &mut self.make_handler().await.recv_msg(addr_len as usize, data_len as usize)
                );
            }
            DatagramSocketRequest::SendMsg { addr, data, control: _, flags: _, responder } => {
                // TODO(brunodalbo) handle control and flags
                // TODO(brunodalbo) we're flattenting the parts of the
                // message in `data` into a single Vec. There may be a
                // way to avoid this by using the FragmentedBuffer
                // utilities in the packet crate.
                responder_send!(
                    responder,
                    &mut self.make_handler().await.send_msg(
                        addr,
                        data.into_iter().map(|v| v.into_iter()).flatten().collect()
                    )
                );
            }
            DatagramSocketRequest::NodeGetFlags { responder } => {
                responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw(), 0);
            }
            DatagramSocketRequest::NodeSetFlags { flags: _, responder } => {
                responder_send!(responder, zx::Status::NOT_SUPPORTED.into_raw());
            }
        }
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
    _marker: std::marker::PhantomData<I>,
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
    fn connect(mut self, addr: Vec<u8>) -> Result<(), libc::c_int> {
        let sockaddr = I::SocketAddress::parse(addr.as_slice()).ok_or(libc::EFAULT)?;
        trace!("connect UDP sockaddr: {:?}", sockaddr);
        if sockaddr.family() != I::SocketAddress::FAMILY {
            return Err(libc::EAFNOSUPPORT);
        }
        let remote_port = sockaddr.get_specified_port().ok_or(libc::ECONNREFUSED)?;
        let remote_addr = sockaddr.get_specified_addr().ok_or(libc::EINVAL)?;

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
            SocketState::BoundConnect { conn_id } => {
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

        self.get_state_mut().info.state = SocketState::BoundConnect { conn_id };
        I::get_collection_mut(self.ctx.dispatcher_mut()).conns.insert(&conn_id, self.binding_id);
        Ok(())
    }

    /// Handles a [POSIX socket bind request].
    ///
    /// [POSIX socket bind request]: psocket::DatagramSocketRequest::Bind
    fn bind(mut self, addr: Vec<u8>) -> Result<(), libc::c_int> {
        let sockaddr = I::SocketAddress::parse(addr.as_slice()).ok_or(libc::EFAULT)?;
        trace!("bind UDP sockaddr: {:?}", sockaddr);
        if sockaddr.family() != I::SocketAddress::FAMILY {
            return Err(libc::EAFNOSUPPORT);
        }
        if self.get_state().info.state.is_bound() {
            return Err(libc::EALREADY);
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
    fn get_sock_name(self) -> Result<Vec<u8>, libc::c_int> {
        match self.get_state().info.state {
            SocketState::Unbound { .. } => {
                return Err(libc::ENOTSOCK);
            }
            SocketState::BoundConnect { conn_id } => {
                let info = get_udp_conn_info(self.ctx.deref(), conn_id);
                Ok(I::SocketAddress::new_vec(*info.local_ip, info.local_port.get()))
            }
            SocketState::BoundListen { listener_id } => {
                let info = get_udp_listener_info(self.ctx.deref(), listener_id);
                let local_ip = match info.local_ip {
                    Some(addr) => *addr,
                    None => I::UNSPECIFIED_ADDRESS,
                };
                Ok(I::SocketAddress::new_vec(local_ip, info.local_port.get()))
            }
        }
    }

    /// Handles a [POSIX socket get_peer_name request].
    ///
    /// [POSIX socket get_peer_name request]: psocket::DatagramSocketRequest::GetPeerName
    fn get_peer_name(self) -> Result<Vec<u8>, libc::c_int> {
        match self.get_state().info.state {
            SocketState::Unbound { .. } => {
                return Err(libc::ENOTSOCK);
            }
            SocketState::BoundListen { .. } => {
                return Err(libc::ENOTCONN);
            }
            SocketState::BoundConnect { conn_id } => {
                let info = get_udp_conn_info(self.ctx.deref(), conn_id);
                Ok(I::SocketAddress::new_vec(*info.remote_ip, info.remote_port.get()))
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
            SocketState::BoundConnect { conn_id } => {
                // remove from bindings:
                I::get_collection_mut(self.ctx.dispatcher_mut()).conns.remove(&conn_id);
                // remove from core:
                remove_udp_conn(self.ctx.deref_mut(), conn_id);
            }
        }
        self.get_state_mut().info.state = SocketState::Unbound;
    }

    fn close(mut self) -> Result<(), libc::c_int> {
        self.close_core();
        Ok(())
    }

    fn recv_msg(
        &mut self,
        addr_len: usize,
        data_len: usize,
    ) -> Result<(Vec<u8>, Vec<u8>, Vec<u8>, u32), libc::c_int> {
        let state = self.get_state_mut();
        let available = if let Some(front) = state.available_data.pop_front() {
            front
        } else {
            return Err(libc::EWOULDBLOCK);
        };
        let addr = if addr_len != 0 {
            let mut v = I::SocketAddress::new_vec(available.source_addr, available.source_port);
            v.truncate(addr_len);
            v
        } else {
            Vec::new()
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
        Ok((addr, data, Vec::new(), truncated.try_into().unwrap_or(std::u32::MAX)))
    }

    fn send_msg(&mut self, addr: Vec<u8>, data: Vec<u8>) -> Result<i64, libc::c_int> {
        let remote = if addr.is_empty() {
            None
        } else {
            let addr = I::SocketAddress::parse(&addr[..]).ok_or(libc::EINVAL)?;
            Some(
                addr.get_specified_addr()
                    .and_then(|a| addr.get_specified_port().map(|p| (a, p)))
                    .ok_or(libc::EINVAL)?,
            )
        };
        let len = data.len() as i64;
        let body = Buf::new(data, ..);
        match self.get_state().info.state {
            SocketState::Unbound => {
                // TODO(brunodalbo) if destination address is set, we should
                // auto-bind here (check POSIX compliance).
                Err(libc::EDESTADDRREQ)
            }
            SocketState::BoundConnect { conn_id } => match remote {
                Some((addr, port)) => {
                    // caller specified a remote socket address, use
                    // stateless UDP send using the local address and port
                    // in `conn_id`.
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
                    // caller did not specify a remote socket address, just use
                    // the existing conn.
                    send_udp_conn::<_, Buf<Vec<u8>>, _>(self.ctx.deref_mut(), conn_id, body)
                        .map_err(IntoErrno::into_errno)
                }
            },
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
                None => Err(libc::EDESTADDRREQ),
            },
        }
        .map(|()| len)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async as fasync;
    use fuchsia_zircon::{self as zx, AsHandleRef};

    use crate::bindings::integration_tests::{
        test_ep_name, StackSetupBuilder, TestSetup, TestSetupBuilder, TestStack,
    };
    use crate::bindings::socket::{
        testutil::{SockAddrTestOptions, TestSockAddr},
        SockAddr4, SockAddr6,
    };
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
            .socket2(A::FAMILY as i16, libc::SOCK_DGRAM as i16, 0)
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
        // pass bad SockAddr struct (too small to be parsed):
        let addr = vec![1_u8, 2, 3, 4];
        let res = proxy.connect(&mut addr.into_iter()).await.unwrap().expect_err("connect fails");
        assert_eq!(res, libc::EFAULT);

        // pass a bad family:
        let addr = A::create_for_test(SockAddrTestOptions {
            port: 1010,
            bad_family: true,
            bad_address: false,
        });
        let res = proxy.connect(&mut addr.into_iter()).await.unwrap().expect_err("connect fails");
        assert_eq!(res, libc::EAFNOSUPPORT);

        // pass an unspecified remote address:
        let addr = A::create_for_test(SockAddrTestOptions {
            port: 1010,
            bad_family: false,
            bad_address: true,
        });
        let res = proxy.connect(&mut addr.into_iter()).await.unwrap().expect_err("connect fails");
        assert_eq!(res, libc::EINVAL);

        // pass a bad port:
        let addr = A::create_for_test(SockAddrTestOptions {
            port: 0,
            bad_family: false,
            bad_address: false,
        });
        let res = proxy.connect(&mut addr.into_iter()).await.unwrap().expect_err("connect fails");
        assert_eq!(res, libc::ECONNREFUSED);

        // pass an unreachable address (tests error forwarding from `udp_connect`):
        let addr = A::create(A::UNREACHABLE_ADDR, 1010);
        let res = proxy.connect(&mut addr.into_iter()).await.unwrap().expect_err("connect fails");
        assert_eq!(res, libc::ENETUNREACH);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_connect_failure_v4() {
        test_udp_connect_failure::<SockAddr4>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_connect_failure_v6() {
        test_udp_connect_failure::<SockAddr6>().await;
    }

    async fn test_udp_connect<A: TestSockAddr>() {
        let (_t, proxy) = udp_prepare_test::<A>().await;
        let remote = A::create(A::REMOTE_ADDR, 200);
        let () = proxy.connect(&mut remote.into_iter()).await.unwrap().expect("connect succeeds");

        // can connect again to a different remote should succeed.
        let remote = A::create(A::REMOTE_ADDR_2, 200);
        let () = proxy.connect(&mut remote.into_iter()).await.unwrap().expect("connect suceeds");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_connect_v4() {
        test_udp_connect::<SockAddr4>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_connect_v6() {
        test_udp_connect::<SockAddr6>().await;
    }

    async fn test_udp_bind<A: TestSockAddr>() {
        let (mut t, socket) = udp_prepare_test::<A>().await;
        let stack = t.get(0);
        // can bind to local address
        let addr = A::create(A::LOCAL_ADDR, 200);
        let () = socket.bind(&mut addr.into_iter()).await.unwrap().expect("bind succeeds");

        // can't bind again (to another port)
        let addr = A::create(A::LOCAL_ADDR, 201);
        let res = socket.bind(&mut addr.into_iter()).await.unwrap().expect_err("bind fails");
        assert_eq!(res, libc::EALREADY);

        // can bind another socket to a different port:
        let socket = get_socket::<A>(stack).await;
        let addr = A::create(A::LOCAL_ADDR, 201);
        let () = socket.bind(&mut addr.into_iter()).await.unwrap().expect("bind succeeds");

        // can bind to unspecified address in a different port:
        let socket = get_socket::<A>(stack).await;
        let addr = A::create(<A::AddrType as IpAddress>::Version::UNSPECIFIED_ADDRESS, 202);
        let () = socket.bind(&mut addr.into_iter()).await.unwrap().expect("bind succeeds");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_bind_v4() {
        test_udp_bind::<SockAddr4>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_bind_v6() {
        test_udp_bind::<SockAddr6>().await;
    }

    async fn test_udp_bind_then_connect<A: TestSockAddr>() {
        let (_t, socket) = udp_prepare_test::<A>().await;
        // can bind to local address
        let bind_addr = A::create(A::LOCAL_ADDR, 200);
        let () = socket.bind(&mut bind_addr.into_iter()).await.unwrap().expect("bind suceeds");

        let remote_addr = A::create(A::REMOTE_ADDR, 1010);
        let () =
            socket.connect(&mut remote_addr.into_iter()).await.unwrap().expect("connect succeeds");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_bind_then_connect_v4() {
        test_udp_bind_then_connect::<SockAddr4>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_bind_then_connect_v6() {
        test_udp_bind_then_connect::<SockAddr6>().await;
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
            alice_socket.get_sock_name().await.unwrap().expect_err("alice getsockname fails"),
            libc::ENOTSOCK
        );
        assert_eq!(
            alice_socket.get_peer_name().await.unwrap().expect_err("alice getpeername fails"),
            libc::ENOTSOCK
        );

        // Setup Alice as a server, bound to LOCAL_ADDR:200
        println!("Configuring alice...");
        let sockaddr = A::create(A::LOCAL_ADDR, 200);
        let () = alice_socket
            .bind(&mut sockaddr.into_iter())
            .await
            .unwrap()
            .expect("alice bind suceeds");

        // Verify that Alice is listening on the local socket, but still has no peer socket
        let want_addr = A::new(A::LOCAL_ADDR, 200);
        assert_eq!(
            alice_socket.get_sock_name().await.unwrap().expect("alice getsockname succeeds"),
            want_addr.as_bytes().to_vec()
        );
        assert_eq!(
            alice_socket.get_peer_name().await.unwrap().expect_err("alice getpeername should fail"),
            libc::ENOTCONN
        );

        // check that alice has no data to read, and it'd block waiting for
        // events:
        assert_eq!(
            alice_socket
                .recv_msg(std::mem::size_of::<A>() as u32, 2048, 0, 0)
                .await
                .unwrap()
                .expect_err("Reading from alice should fail"),
            libc::EWOULDBLOCK
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
        let sockaddr = A::create(A::REMOTE_ADDR, 300);
        let () =
            bob_socket.bind(&mut sockaddr.into_iter()).await.unwrap().expect("bob bind suceeds");

        // Verify that Bob is listening on the local socket, but has no peer socket
        let want_addr = A::new(A::REMOTE_ADDR, 300);
        assert_eq!(
            bob_socket.get_sock_name().await.unwrap().expect("bob getsockname suceeds"),
            want_addr.as_bytes().to_vec()
        );
        assert_eq!(
            bob_socket
                .get_peer_name()
                .await
                .unwrap()
                .expect_err("get peer name should fail before connected"),
            libc::ENOTCONN
        );

        // Connect Bob to Alice on LOCAL_ADDR:200
        println!("Connecting bob to alice...");
        let sockaddr = A::create(A::LOCAL_ADDR, 200);
        let () =
            bob_socket.connect(&mut sockaddr.into_iter()).await.unwrap().expect("Connect succeeds");

        // Verify that Bob has the peer socket set correctly
        let want_addr = A::new(A::LOCAL_ADDR, 200);
        assert_eq!(
            bob_socket.get_peer_name().await.unwrap().expect("bob getpeername suceeds"),
            want_addr.as_bytes().to_vec()
        );

        // We don't care which signals are on, only that SIGNAL_OUTGOING is, we
        // can ignore the return value.
        let _signals = bob_events
            .wait_handle(ZXSIO_SIGNAL_OUTGOING, zx::Time::from_nanos(0))
            .expect("Bob outgoing event should be signaled");

        // Send datagram from Bob's socket.
        println!("Writing datagram to bob");
        let body = "Hello".as_bytes();
        let mut body_iter = body.iter().copied();
        let body_iter: Option<&mut dyn ExactSizeIterator<Item = u8>> = Some(&mut body_iter);
        assert_eq!(
            bob_socket
                .send_msg(
                    &mut None.into_iter(),
                    &mut body_iter.into_iter(),
                    &mut None.into_iter(),
                    0
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
            .recv_msg(std::mem::size_of::<A>() as u32, 2048, 0, 0)
            .await
            .unwrap()
            .expect("recvmsg suceeeds");
        let source = A::parse(&from[..]).expect("can parse sockaddr");
        assert_eq!(source.addr(), A::REMOTE_ADDR);
        assert_eq!(source.port(), 300);
        assert_eq!(truncated, 0);
        assert_eq!(&data[..], body);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_hello_v4() {
        test_udp_hello::<SockAddr4>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_hello_v6() {
        test_udp_hello::<SockAddr6>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_socket_describe() {
        let mut t = TestSetupBuilder::new().add_endpoint().add_empty_stack().build().await.unwrap();
        let test_stack = t.get(0);
        let socket_provider = test_stack.connect_socket_provider().unwrap();
        let socket = socket_provider
            .socket2(libc::AF_INET as i16, libc::SOCK_DGRAM as i16, 0)
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
}
