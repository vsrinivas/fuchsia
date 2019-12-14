// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! UDP socket bindings.

use std::future::Future;
use std::num::NonZeroU16;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll};

use failure::{format_err, Error};
use fidl_fuchsia_posix_socket as psocket;
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, prelude::HandleBased};
use futures::{channel::mpsc, channel::oneshot, future::Either, TryFutureExt, TryStreamExt};
use log::{debug, error, trace};
use net_types::ip::{Ip, IpAddress, IpVersion, Ipv4, Ipv6};
use netstack3_core::{
    connect_udp, get_udp_conn_info, get_udp_listener_info, icmp::IcmpIpExt, listen_udp,
    remove_udp_conn, remove_udp_listener, send_udp, send_udp_conn, send_udp_listener,
    IdMapCollection, LocalAddressError, RemoteAddressError, SocketError, UdpConnId,
    UdpEventDispatcher, UdpListenerId,
};
use packet::{serialize::Buf, BufferView};
use zerocopy::{AsBytes, LayoutVerified};

use crate::{
    eventloop::{Event, EventLoopInner},
    EventLoop,
};

use super::{
    get_domain_ip_version, FdioSocketMsg, IpSockAddrExt, SockAddr, SocketEventInner,
    SocketWorkerProperties,
};

#[derive(Default)]
pub(crate) struct UdpSocketCollection {
    v4: UdpSocketCollectionInner<Ipv4>,
    v6: UdpSocketCollectionInner<Ipv6>,
}

#[derive(Default)]
pub(crate) struct UdpSocketCollectionInner<I: Ip> {
    conns: IdMapCollection<UdpConnId<I>, Arc<Mutex<SocketWorkerInner<I>>>>,
    listeners: IdMapCollection<UdpListenerId<I>, Arc<Mutex<SocketWorkerInner<I>>>>,
}

/// Extension trait for [`Ip`] for UDP sockets operations.
pub(crate) trait UdpSocketIpExt: IpSockAddrExt + IcmpIpExt {
    fn get_collection(col: &UdpSocketCollection) -> &UdpSocketCollectionInner<Self>;
    fn get_collection_mut(col: &mut UdpSocketCollection) -> &mut UdpSocketCollectionInner<Self>;
}

impl UdpSocketIpExt for Ipv4 {
    fn get_collection(col: &UdpSocketCollection) -> &UdpSocketCollectionInner<Self> {
        &col.v4
    }

    fn get_collection_mut(col: &mut UdpSocketCollection) -> &mut UdpSocketCollectionInner<Self> {
        &mut col.v4
    }
}

impl UdpSocketIpExt for Ipv6 {
    fn get_collection(col: &UdpSocketCollection) -> &UdpSocketCollectionInner<Self> {
        &col.v6
    }

    fn get_collection_mut(col: &mut UdpSocketCollection) -> &mut UdpSocketCollectionInner<Self> {
        &mut col.v6
    }
}

// NOTE(brunodalbo) we implement UdpEventDispatcher for EventLoopInner in this
// module so it's closer to the rest of the UDP logic
impl<I: UdpSocketIpExt> UdpEventDispatcher<I> for EventLoopInner {
    fn receive_udp_from_conn(
        &mut self,
        conn: UdpConnId<I>,
        src_ip: I::Addr,
        src_port: NonZeroU16,
        body: &[u8],
    ) {
        let worker = I::get_collection(&self.udp_sockets).conns.get(&conn).unwrap().lock().unwrap();
        if let Err(e) = worker.receive_datagram(src_ip, src_port.get(), body) {
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
        let worker =
            I::get_collection(&self.udp_sockets).listeners.get(&listener).unwrap().lock().unwrap();
        if let Err(e) =
            worker.receive_datagram(src_ip, src_port.map(|p| p.get()).unwrap_or(0), body)
        {
            error!("receive_udp_from_conn failed: {:?}", e);
        }
    }
}

/// A socket worker for either an IPv4 or IPv6 socket.
#[derive(Debug, Clone)]
enum SocketWorkerEither {
    V4(Arc<Mutex<SocketWorkerInner<Ipv4>>>),
    V6(Arc<Mutex<SocketWorkerInner<Ipv6>>>),
}

/// Worker that serves the fuchsia.posix.socket.Control FIDL.
pub(super) struct UdpSocketWorker {
    events: psocket::ControlRequestStream,
    inner: SocketWorkerEither,
}

/// Internal state of a [`UdpSocketWorker`].
#[derive(Debug)]
struct SocketWorkerInner<I: Ip> {
    local_socket: zx::Socket,
    peer_socket: zx::Socket,
    info: SocketControlInfo<I>,
}

/// A future that represents the data-handling signal of a
/// [`SocketWorkerInner`].
///
/// The two variants represents the two states of handling each datagram; the
/// `Signals` variant is a future that is waiting for the next datagram on the
/// `SocketWorkerInner`'s local socket. The `Return` variant is a future that is
/// waiting for a `SocketWorkerInner` to finish processing a datagram.
enum DataWorkerFut {
    /// Wait for the provided [`fasync::OnSignals`] future, obtained from
    /// [`SocketWorkerInner::create_readable_signal`].
    Signals(fasync::OnSignals<'static>),
    /// Wait on the provided [`oneshot::Receiver`], whose sender end is sent to
    /// a `SocketWorkerInner` as a [`UdpSocketEvent`] until the data in the
    /// `SocketWorkerInner`'s local socket is handled.
    Return(oneshot::Receiver<()>),
}

impl Future for DataWorkerFut {
    type Output = Option<Result<zx::Signals, zx::Status>>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        match self.get_mut() {
            DataWorkerFut::Return(rx) => {
                futures::pin_mut!(rx);
                rx.poll(cx).map(|_| None)
            }
            DataWorkerFut::Signals(s) => {
                futures::pin_mut!(s);
                s.poll(cx).map(|s| Some(s))
            }
        }
    }
}

/// The types of events carried by a [`UdpSocketEvent`].
#[derive(Debug)]
enum UdpSocketEventInner {
    /// A FIDL request on this socket's control plane.
    ControlRequest(psocket::ControlRequest),
    /// A request for the [`SocketWorkerInner`] to read a datagram from its
    /// local socket.
    ///
    /// Once the datagram is processed, the contained [`oneshot::Sender`] must
    /// be signalled.
    DataRequest(oneshot::Sender<()>),
    /// A request to destroy a [`UdpSocketWorker`], and dispose of all its
    /// resources.
    Destroy,
}

impl UdpSocketEventInner {
    fn handle<I: UdpSocketIpExt>(
        self,
        event_loop: &mut EventLoop,
        worker: Arc<Mutex<SocketWorkerInner<I>>>,
    ) {
        match self {
            UdpSocketEventInner::ControlRequest(request) => {
                worker.lock().unwrap().handle_request(event_loop, request, &worker)
            }
            UdpSocketEventInner::DataRequest(sender) => {
                if let Err(e) = worker.lock().unwrap().handle_socket_data(event_loop) {
                    // TODO(brunodalbo) are there errors where we should close
                    // the socket?
                    error!("UDP: failed to handle socket data: {:?}", e);
                }
                sender.send(()).unwrap();
            }
            UdpSocketEventInner::Destroy => {
                worker.lock().unwrap().teardown(event_loop);
            }
        }
    }
}

/// A socket event for UDP sockets
#[derive(Debug)]
pub(super) struct UdpSocketEvent {
    worker: SocketWorkerEither,
    inner: UdpSocketEventInner,
}

impl UdpSocketEvent {
    /// Handles this `UdpSocketEvent` on the provided `event_loop`
    /// context.
    pub(super) fn handle_event(self, event_loop: &mut EventLoop) {
        match self.worker {
            SocketWorkerEither::V4(sw) => self.inner.handle(event_loop, sw),
            SocketWorkerEither::V6(sw) => self.inner.handle(event_loop, sw),
        }
    }
}

impl From<UdpSocketEvent> for Event {
    fn from(req: UdpSocketEvent) -> Self {
        SocketEventInner::Udp(req).into()
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

impl UdpSocketWorker {
    /// Creates a new `UdpSocketWorker` with the provided arguments.
    ///
    /// The `UdpSocketWorker` takes control of the event stream in
    /// `events`. To start servicing the events, see
    /// [`UdpSocketWorker::spawn`].
    pub fn new(
        ip_version: IpVersion,
        events: psocket::ControlRequestStream,
        properties: SocketWorkerProperties,
    ) -> Result<Self, libc::c_int> {
        let (local_socket, peer_socket) =
            zx::Socket::create(zx::SocketOpts::DATAGRAM).map_err(|_| libc::ENOBUFS)?;
        Ok(Self {
            events,
            inner: match ip_version {
                IpVersion::V4 => SocketWorkerEither::V4(Arc::new(Mutex::new(
                    SocketWorkerInner::new(local_socket, peer_socket, properties),
                ))),
                IpVersion::V6 => SocketWorkerEither::V6(Arc::new(Mutex::new(
                    SocketWorkerInner::new(local_socket, peer_socket, properties),
                ))),
            },
        })
    }

    /// Starts servicing events from the event stream this
    /// `UdpSocketWorker` was created with.
    ///
    /// Socket control events will be sent to the receiving end of `sender` as
    /// [`Event::SocketEvent`] variants.
    pub fn spawn(mut self, sender: mpsc::UnboundedSender<Event>) {
        fasync::spawn_local(
            async move {
                let r = self.loop_events(&sender).await;
                trace!("UDP SocketWorker event loop finished {:?}", r);
                // when socket is destroyed, we need to clean things up:
                if let Err(e) = sender.unbounded_send(
                    UdpSocketEvent { worker: self.inner, inner: UdpSocketEventInner::Destroy }
                        .into(),
                ) {
                    error!("Error sending destroy event to loop: {:?}", e);
                }
                r
            }
            // When the closure above finishes, that means `self` goes out
            // of scope and is dropped, meaning that the event stream's
            // underlying channel is closed.
            // If any errors occured as a result of the closure, we just log
            // them.
            .unwrap_or_else(|e: Error| error!("UDP socket control request error: {:?}", e)),
        );
    }

    /// Services internal event streams until `self.events` finishes or an
    /// unrecoverable error occurs.
    ///
    /// `loop_events` services two event streams: `self.events`, which is a
    /// stream of requests over the POSIX FIDL channel, and a [`DataWorkerFut`]
    /// signal, which processes readable data in the [`SocketWorkerInner`]'s
    /// `local_socket` and waits for processing of such data.
    async fn loop_events(&mut self, sender: &mpsc::UnboundedSender<Event>) -> Result<(), Error> {
        let mut dw_fut = DataWorkerFut::Signals(match &self.inner {
            SocketWorkerEither::V4(sw) => sw.lock().unwrap().create_readable_signal(),
            SocketWorkerEither::V6(sw) => sw.lock().unwrap().create_readable_signal(),
        });
        let mut evt_fut = self.events.try_next();
        loop {
            match futures::future::select(evt_fut, dw_fut).await {
                Either::Left((evt, nxt)) => {
                    if let Some(request) = evt? {
                        let () = Self::handle_control_request(&self.inner, &sender, request)?;
                    } else {
                        // return Ok, request stream was closed.
                        return Ok(());
                    }
                    evt_fut = self.events.try_next();
                    dw_fut = nxt;
                }
                Either::Right((dw, nxt)) => {
                    dw_fut = Self::handle_data_request(&self.inner, &sender, dw)?;
                    evt_fut = nxt;
                }
            }
        }
    }

    /// Handles a single control request, fetched from the POSIX FIDL channel in
    /// `self.events`.
    fn handle_control_request(
        inner: &SocketWorkerEither,
        sender: &mpsc::UnboundedSender<Event>,
        request: psocket::ControlRequest,
    ) -> Result<(), Error> {
        let () = sender
            .unbounded_send(
                UdpSocketEvent {
                    worker: inner.clone(),
                    inner: UdpSocketEventInner::ControlRequest(request),
                }
                .into(),
            )
            .map_err(|e| format_err!("Failed to send socket ControlRequest {:?}", e))?;
        Ok(())
    }

    /// Handles a single data request signal, obtained from a [`DataWorkerFut`].
    ///
    /// If `request` is `None`, the [`SocketWorkerInner`] is ready to process
    /// the next datagram, so `handle_data_request` returns a
    /// [`DataWorkerFut::Signals`] variant, to wait for the next data readable
    /// signal on the local socket.
    ///
    /// If `request` is `Some`, a [`UdpSocketEventInner::DataRequest`] is
    /// created and sent over the `sender` channel for the data to be processed
    /// by `SocketWorkerInner`, and `handle_data_request` returns a
    /// [`DataWorkerFut::Return`] future that will be completed once the pending
    /// datagram is processed.
    fn handle_data_request(
        inner: &SocketWorkerEither,
        sender: &mpsc::UnboundedSender<Event>,
        request: Option<Result<zx::Signals, zx::Status>>,
    ) -> Result<DataWorkerFut, Error> {
        Ok(match request {
            None => DataWorkerFut::Signals(match inner {
                SocketWorkerEither::V4(sw) => sw.lock().unwrap().create_readable_signal(),
                SocketWorkerEither::V6(sw) => sw.lock().unwrap().create_readable_signal(),
            }),
            Some(signals) => {
                // we're just listening for SOCKET_READABLE,
                // so we don't care much about what signals
                // triggered it.
                let _ =
                    signals.map_err(|e| format_err!("Error waiting on socket signals: {:?}", e))?;

                let (snd, rcv) = oneshot::channel();
                let () = sender
                    .unbounded_send(
                        UdpSocketEvent {
                            worker: inner.clone(),
                            inner: UdpSocketEventInner::DataRequest(snd),
                        }
                        .into(),
                    )
                    .map_err(|e| format_err!("Failed to send socket data request {:?}", e))?;
                DataWorkerFut::Return(rcv)
            }
        })
    }
}

impl<I: UdpSocketIpExt> SocketWorkerInner<I> {
    /// Creates a new `SocketWorkerInner` with the provided socket pair and
    /// `properties`.
    fn new(
        local_socket: zx::Socket,
        peer_socket: zx::Socket,
        properties: SocketWorkerProperties,
    ) -> Self {
        Self {
            local_socket,
            peer_socket,
            info: SocketControlInfo { properties, state: SocketState::Unbound },
        }
    }

    /// Creates an [`fasync::OnSignals`] future for when the local socket is
    /// readable.
    fn create_readable_signal(&self) -> fasync::OnSignals<'static> {
        fasync::OnSignals::new(&self.local_socket, zx::Signals::SOCKET_READABLE).extend_lifetime()
    }

    /// Handles a [POSIX socket connect request].
    ///
    /// [POSIX socket connect request]: psocket::ControlRequest::Connect
    fn connect(
        &mut self,
        event_loop: &mut EventLoop,
        addr: Vec<u8>,
        arc_self: &Arc<Mutex<Self>>,
    ) -> Result<(), libc::c_int> {
        let sockaddr = I::SocketAddress::parse(addr.as_slice()).ok_or(libc::EFAULT)?;
        trace!("connect UDP sockaddr: {:?}", sockaddr);
        if sockaddr.family() != I::SocketAddress::FAMILY {
            return Err(libc::EAFNOSUPPORT);
        }
        let remote_port = sockaddr.get_specified_port().ok_or(libc::ECONNREFUSED)?;
        let remote_addr = sockaddr.get_specified_addr().ok_or(libc::EINVAL)?;

        let (local_addr, local_port) = match self.info.state {
            SocketState::Unbound => {
                // do nothing, we're already unbound.
                // return None for local_addr and local_port.
                (None, None)
            }
            SocketState::BoundListen { listener_id } => {
                // if we're bound to a listen mode, we need to remove the
                // listener, and retrieve the bound local addr and port.
                let list_info = remove_udp_listener(&mut event_loop.ctx, listener_id);
                // also remove from the EventLoop context:
                I::get_collection_mut(&mut event_loop.ctx.dispatcher_mut().udp_sockets)
                    .listeners
                    .remove(&listener_id)
                    .unwrap();

                (list_info.local_ip, Some(list_info.local_port))
            }
            SocketState::BoundConnect { conn_id } => {
                // if we're bound to a connect mode, we need to remove the
                // connection, and retrieve the bound local addr and port.
                let conn_info = remove_udp_conn(&mut event_loop.ctx, conn_id);
                // also remove from the EventLoop context:
                I::get_collection_mut(&mut event_loop.ctx.dispatcher_mut().udp_sockets)
                    .conns
                    .remove(&conn_id)
                    .unwrap();
                (Some(conn_info.local_ip), Some(conn_info.local_port))
            }
        };

        // TODO(maufflick): convert connect_udp result response to appropriate libc::c_int values
        // for `connect`.
        let conn_id =
            connect_udp(&mut event_loop.ctx, local_addr, local_port, remote_addr, remote_port)
                .map_err(SocketError::into_errno)?;

        self.info.state = SocketState::BoundConnect { conn_id };
        I::get_collection_mut(&mut event_loop.ctx.dispatcher_mut().udp_sockets)
            .conns
            .insert(&conn_id, Arc::clone(arc_self));
        Ok(())
    }

    /// Handles a [POSIX socket bind request]
    ///
    /// [POSIX socket bind request]: psocket::ControlRequest::Bind
    fn bind(
        &mut self,
        event_loop: &mut EventLoop,
        addr: Vec<u8>,
        arc_self: &Arc<Mutex<Self>>,
    ) -> Result<(), libc::c_int> {
        let sockaddr = I::SocketAddress::parse(addr.as_slice()).ok_or(libc::EFAULT)?;
        trace!("bind UDP sockaddr: {:?}", sockaddr);
        if sockaddr.family() != I::SocketAddress::FAMILY {
            return Err(libc::EAFNOSUPPORT);
        }
        if self.info.state.is_bound() {
            return Err(libc::EALREADY);
        }
        let local_addr = sockaddr.get_specified_addr();
        let local_port = sockaddr.get_specified_port();

        let listener_id = listen_udp(&mut event_loop.ctx, local_addr, local_port)
            .map_err(SocketError::into_errno)?;
        self.info.state = SocketState::BoundListen { listener_id };
        I::get_collection_mut(&mut event_loop.ctx.dispatcher_mut().udp_sockets)
            .listeners
            .insert(&listener_id, Arc::clone(arc_self));
        Ok(())
    }

    /// Handles a [POSIX socket get_sock_name request].
    ///
    /// [POSIX socket get_sock_name request]: psocket::ControlRequest::GetSockName
    fn get_sock_name(
        &mut self,
        event_loop: &mut EventLoop,
    ) -> Result<I::SocketAddress, libc::c_int> {
        match self.info.state {
            SocketState::Unbound { .. } => {
                return Err(libc::ENOTSOCK);
            }
            SocketState::BoundConnect { conn_id } => {
                let info = get_udp_conn_info(&event_loop.ctx, conn_id);
                Ok(I::SocketAddress::new(*info.local_ip, info.local_port.get()))
            }
            SocketState::BoundListen { listener_id } => {
                let info = get_udp_listener_info(&event_loop.ctx, listener_id);
                let local_ip = match info.local_ip {
                    Some(addr) => *addr,
                    None => I::UNSPECIFIED_ADDRESS,
                };
                Ok(I::SocketAddress::new(local_ip, info.local_port.get()))
            }
        }
    }

    /// Handles a [POSIX socket get_peer_name request].
    ///
    /// [POSIX socket get_peer_name request]: psocket::ControlRequest::GetPeerName
    fn get_peer_name(
        &mut self,
        event_loop: &mut EventLoop,
    ) -> Result<I::SocketAddress, libc::c_int> {
        match self.info.state {
            SocketState::Unbound { .. } => {
                return Err(libc::ENOTSOCK);
            }
            SocketState::BoundListen { .. } => {
                return Err(libc::ENOTCONN);
            }
            SocketState::BoundConnect { conn_id } => {
                let info = get_udp_conn_info(&event_loop.ctx, conn_id);
                Ok(I::SocketAddress::new(*info.remote_ip, info.remote_port.get()))
            }
        }
    }

    /// Handles a [POSIX socket request].
    ///
    /// [POSIX socket request]: psocket::ControlRequest
    // TODO(fxb/37419): Remove default handling after methods landed.
    #[allow(unreachable_patterns)]
    fn handle_request(
        &mut self,
        event_loop: &mut EventLoop,
        req: psocket::ControlRequest,
        arc_self: &Arc<Mutex<Self>>,
    ) {
        match req {
            psocket::ControlRequest::Describe { responder } => {
                let peer = self.peer_socket.duplicate_handle(zx::Rights::SAME_RIGHTS);
                if let Ok(peer) = peer {
                    let mut info =
                        fidl_fuchsia_io::NodeInfo::Socket(fidl_fuchsia_io::Socket { socket: peer });
                    responder_send!(responder, &mut info);
                }
                // If the call to duplicate_handle fails, we have no choice but to drop the
                // responder and close the channel, since Describe must be infallible.
            }
            psocket::ControlRequest::Connect { addr, responder } => {
                responder_send!(
                    responder,
                    self.connect(event_loop, addr, arc_self).err().unwrap_or(0) as i16
                );
            }
            psocket::ControlRequest::Clone { .. } => {}
            psocket::ControlRequest::Close { .. } => {}
            psocket::ControlRequest::Sync { .. } => {}
            psocket::ControlRequest::GetAttr { .. } => {}
            psocket::ControlRequest::SetAttr { .. } => {}
            psocket::ControlRequest::Bind { addr, responder } => responder_send!(
                responder,
                self.bind(event_loop, addr, arc_self).err().unwrap_or(0) as i16
            ),
            psocket::ControlRequest::Listen { .. } => {}
            psocket::ControlRequest::Accept { .. } => {}
            psocket::ControlRequest::GetSockName { responder } => {
                match self.get_sock_name(event_loop) {
                    Ok(sock_name) => {
                        responder_send!(responder, 0i16, &mut sock_name.as_bytes().iter().copied());
                    }
                    Err(err) => {
                        responder_send!(responder, err as i16, &mut None.into_iter());
                    }
                }
            }
            psocket::ControlRequest::GetPeerName { responder } => {
                match self.get_peer_name(event_loop) {
                    Ok(sock_name) => {
                        responder_send!(responder, 0i16, &mut sock_name.as_bytes().iter().copied());
                    }
                    Err(err) => {
                        responder_send!(responder, err as i16, &mut None.into_iter());
                    }
                }
            }
            psocket::ControlRequest::SetSockOpt { .. } => {}
            psocket::ControlRequest::GetSockOpt { .. } => {}
            _ => {}
        }
    }

    /// Handles a pending datagram in `local_socket`.
    // NOTE(brunodalbo): upcoming changes to how we operate UDP sockets with
    // FDIO will render this function obsolete, UDP datagrams will reach
    // netstack through FIDL.
    fn handle_socket_data(&mut self, event_loop: &mut EventLoop) -> Result<(), Error> {
        // reserve a read buffer with 64k + message header.
        let mut buff = [0; (1 << 16) + std::mem::size_of::<FdioSocketMsg>()];
        let len = self
            .local_socket
            .read(&mut buff[..])
            .map_err(|e| format_err!("UDP failed to read from socket: {:?}", e))?;
        let mut buff = &mut buff[..len];
        let mut bv = &mut buff;
        let hdr: LayoutVerified<&mut [u8], FdioSocketMsg> =
            bv.take_obj_front().ok_or_else(|| format_err!("Failed to parse UDP FDIO header"))?;
        let data: &mut [u8] = bv.into_rest();
        let _ = get_domain_ip_version(hdr.addr.family.get())
            .ok_or_else(|| format_err!("Invalid family in FDIO header"))
            .and_then(|ip_version| {
                if ip_version == I::VERSION {
                    Ok(())
                } else {
                    Err(format_err!("Incompatible family in FDIO header"))
                }
            })?;
        let len = hdr.addrlen.get() as usize;
        // TODO(brunodalbo) use flags
        let _flags = hdr.flags.get();
        let addr_data = &hdr.bytes()[..len];

        let sock_addr = I::SocketAddress::parse(addr_data)
            .ok_or_else(|| format_err!("Failed to parse sockaddr {:?}", addr_data))?;
        let body = Buf::new(data, ..);
        let remote_addr = sock_addr.get_specified_addr();
        let remote_port = sock_addr.get_specified_port();
        match self.info.state {
            SocketState::BoundConnect { conn_id } => match (remote_addr, remote_port) {
                (Some(addr), Some(port)) => {
                    // this is a "sendto" call, use stateless UDP send using the
                    // local address and port in `conn_id`.
                    let conn_info = get_udp_conn_info(&event_loop.ctx, conn_id);
                    send_udp::<I, _, _>(
                        &mut event_loop.ctx,
                        Some(conn_info.local_ip),
                        Some(conn_info.local_port),
                        addr,
                        port,
                        body,
                    )?;
                }
                (None, None) => {
                    // this is not a "sendto" call, just use the existing conn:
                    send_udp_conn(&mut event_loop.ctx, conn_id, body)?;
                }
                _ => {
                    return Err(format_err!("Unspecified address or port in UDP connection socket"))
                }
            },
            SocketState::BoundListen { listener_id } => {
                // remote addr and remote port need both to be specified,
                // otherwise we can't send from listener
                let (remote_addr, remote_port) = match (remote_addr, remote_port) {
                    (Some(a), Some(p)) => (a, p),
                    _ => {
                        return Err(format_err!(
                            "Unspecified address or port in UDP listening socket"
                        ));
                    }
                };
                let _ = send_udp_listener(
                    &mut event_loop.ctx,
                    listener_id,
                    None,
                    remote_addr,
                    remote_port,
                    body,
                )?;
            }
            // we shouldn't be handling socket data before a bound state:
            SocketState::Unbound => {
                // TODO(brunodalbo): Sending data in Unbound state can result in
                // one of two things: if it's a sendto, the socket will
                // auto-bind and then send the data, if it's a regular send, we
                // should fail with an error. The API does not allow us to
                // return errors yet, so we're just going to panic for now.
                panic!("Sending data on unbound socket is not supported");
            }
        }

        Ok(())
    }

    fn teardown(&mut self, event_loop: &mut EventLoop) {
        match self.info.state {
            SocketState::Unbound => {} // nothing to do
            SocketState::BoundListen { listener_id } => {
                // remove from bindings:
                I::get_collection_mut(&mut event_loop.ctx.dispatcher_mut().udp_sockets)
                    .listeners
                    .remove(&listener_id);
                // remove from core:
                remove_udp_listener(&mut event_loop.ctx, listener_id);
            }
            SocketState::BoundConnect { conn_id } => {
                // remove from bindings:
                I::get_collection_mut(&mut event_loop.ctx.dispatcher_mut().udp_sockets)
                    .conns
                    .remove(&conn_id);
                // remove from core:
                remove_udp_conn(&mut event_loop.ctx, conn_id);
            }
        }
    }

    fn receive_datagram(&self, addr: I::Addr, port: u16, body: &[u8]) -> Result<(), Error> {
        let data = make_datagram::<I::SocketAddress>(addr, port, body);
        self.local_socket
            .write(&data[..])
            // local_socket is a datagram socket, so we don't really care about
            // the return of write, it either succeeds or it fails.
            .map(|_| ())
            .map_err(|status| format_err!("UDP failed writing to user socket: {:?}", status))
    }
}

/// Makes an FDIO datagram message (for compatibility with recvfrom) with the
/// provided arguments.
///
/// The datagram consists of a serialized [`FdioSocketMsg`] created from `addr`
/// and `port` followed by `body`.
// NOTE(brunodalbo): upcoming changes to how we operate UDP sockets with FDIO
// will render this function obsolete. Right now its performance is really
// lacking, requiring a heap allocation to be able to inline the address and
// port information in an `FdioSocketMsg`. We're not too worried about the
// performance, given this is going away soon.
fn make_datagram<A: SockAddr>(addr: A::AddrType, port: u16, body: &[u8]) -> Vec<u8> {
    let mut data = Vec::with_capacity(body.len() + std::mem::size_of::<FdioSocketMsg>());
    let mut sock_msg = FdioSocketMsg::default();
    let addrlen = std::mem::size_of::<A>();
    sock_msg.addrlen.set(addrlen as u32);
    let mut sockaddr = LayoutVerified::<_, A>::new(&mut sock_msg.as_bytes_mut()[..addrlen])
        .expect("Addrlen must match");
    sockaddr.set_family(A::FAMILY);
    sockaddr.set_port(port);
    sockaddr.set_addr(addr.bytes());

    data.extend_from_slice(sock_msg.as_bytes());
    data.extend_from_slice(body);
    data
}

/// Trait expressing the conversion of error types into `libc::c_int` errno-like errors for the
/// POSIX-lite wrappers.
trait IntoErrno {
    /// Returns the most equivalent POSIX error code for `self`.
    fn into_errno(self) -> libc::c_int;
}

impl IntoErrno for SocketError {
    /// Converts Fuchsia `SocketError` errors into the most equivalent POSIX error.
    fn into_errno(self) -> libc::c_int {
        match self {
            SocketError::Remote(e) => match e {
                RemoteAddressError::NoRoute => libc::ENETUNREACH,
            },
            SocketError::Local(e) => match e {
                LocalAddressError::CannotBindToAddress
                | LocalAddressError::FailedToAllocateLocalPort => libc::EADDRNOTAVAIL,
                LocalAddressError::AddressMismatch => libc::EINVAL,
                LocalAddressError::AddressInUse => libc::EADDRINUSE,
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async as fasync;

    use crate::eventloop::integration_tests::{
        test_ep_name, StackSetupBuilder, TestSetup, TestSetupBuilder, TestStack,
    };
    use crate::eventloop::socket::{
        testutil::{SockAddrTestOptions, TestSockAddr},
        SockAddr4, SockAddr6,
    };
    use net_types::ip::{Ip, IpAddress};

    fn make_unspecified_remote_datagram<A: SockAddr>(body: &[u8]) -> Vec<u8> {
        make_datagram::<A>(<A::AddrType as IpAddress>::Version::UNSPECIFIED_ADDRESS, 0, body)
    }

    async fn udp_prepare_test<A: TestSockAddr>() -> (TestSetup, psocket::ControlProxy) {
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

    async fn get_socket<A: TestSockAddr>(test_stack: &mut TestStack) -> psocket::ControlProxy {
        let socket_provider = test_stack.connect_socket_provider().unwrap();
        let socket_response = test_stack
            .run_future(socket_provider.socket(A::FAMILY as i16, libc::SOCK_DGRAM as i16, 0))
            .await
            .expect("Socket call succeeds");
        assert_eq!(socket_response.0, 0);
        socket_response.1.expect("Socket returns a channel").into_proxy().unwrap()
    }

    async fn get_socket_data_plane<A: TestSockAddr>(
        test_stack: &mut TestStack,
    ) -> (psocket::ControlProxy, zx::Socket) {
        let ctlr = get_socket::<A>(test_stack).await;
        let node_info =
            test_stack.run_future(ctlr.describe()).await.expect("Socked describe succeeds");
        let sock = match node_info {
            fidl_fuchsia_io::NodeInfo::Socket(s) => s.socket,
            _ => panic!("Got wrong describe response for UDP socket"),
        };
        (ctlr, sock)
    }

    async fn test_udp_connect_failure<A: TestSockAddr>() {
        let (mut t, proxy) = udp_prepare_test::<A>().await;
        let stack = t.get(0);
        // pass bad SockAddr struct (too small to be parsed):
        let addr = vec![1_u8, 2, 3, 4];
        let res = stack.run_future(proxy.connect(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, libc::EFAULT);

        // pass a bad family:
        let addr = A::create_for_test(SockAddrTestOptions {
            port: 1010,
            bad_family: true,
            bad_address: false,
        });
        let res = stack.run_future(proxy.connect(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, libc::EAFNOSUPPORT);

        // pass an unspecified remote address:
        let addr = A::create_for_test(SockAddrTestOptions {
            port: 1010,
            bad_family: false,
            bad_address: true,
        });
        let res = stack.run_future(proxy.connect(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, libc::EINVAL);

        // pass a bad port:
        let addr = A::create_for_test(SockAddrTestOptions {
            port: 0,
            bad_family: false,
            bad_address: false,
        });
        let res = stack.run_future(proxy.connect(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, libc::ECONNREFUSED);

        // pass an unreachable address (tests error forwarding from `udp_connect`):
        let addr = A::create(A::UNREACHABLE_ADDR, 1010);
        let res = stack.run_future(proxy.connect(&mut addr.into_iter())).await.unwrap() as i32;
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
        let (mut t, proxy) = udp_prepare_test::<A>().await;
        let stack = t.get(0);
        let remote = A::create(A::REMOTE_ADDR, 200);
        let res = stack.run_future(proxy.connect(&mut remote.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);

        // can connect again to a different remote should succeed.
        let remote = A::create(A::REMOTE_ADDR_2, 200);
        let res = stack.run_future(proxy.connect(&mut remote.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);
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
        let res = stack.run_future(socket.bind(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);

        // can't bind again (to another port)
        let addr = A::create(A::LOCAL_ADDR, 201);
        let res = stack.run_future(socket.bind(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, libc::EALREADY);

        // can bind another socket to a different port:
        let socket = get_socket::<A>(stack).await;
        let addr = A::create(A::LOCAL_ADDR, 201);
        let res = stack.run_future(socket.bind(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);

        // can bind to unspecified address in a different port:
        let socket = get_socket::<A>(stack).await;
        let addr = A::create(<A::AddrType as IpAddress>::Version::UNSPECIFIED_ADDRESS, 202);
        let res = stack.run_future(socket.bind(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);
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
        let (mut t, socket) = udp_prepare_test::<A>().await;
        let stack = t.get(0);
        // can bind to local address
        let bind_addr = A::create(A::LOCAL_ADDR, 200);
        let res = stack.run_future(socket.bind(&mut bind_addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);
        let remote_addr = A::create(A::REMOTE_ADDR, 1010);
        let res =
            stack.run_future(socket.connect(&mut remote_addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);
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
        let (alice_ctl, alice_sock) = get_socket_data_plane::<A>(alice).await;

        // Verify that Alice has no local or peer addresses bound
        assert_eq!(
            alice.run_future(alice_ctl.get_sock_name()).await.unwrap(),
            (libc::ENOTSOCK as i16, None.into_iter().collect::<Vec<u8>>())
        );
        assert_eq!(
            alice.run_future(alice_ctl.get_peer_name()).await.unwrap(),
            (libc::ENOTSOCK as i16, None.into_iter().collect::<Vec<u8>>())
        );

        // Setup Alice as a server, bound to LOCAL_ADDR:200
        println!("Configuring alice...");
        let sockaddr = A::create(A::LOCAL_ADDR, 200);
        assert_eq!(
            alice.run_future(alice_ctl.bind(&mut sockaddr.into_iter())).await.unwrap() as i32,
            0
        );

        // Verify that Alice is listening on the local socket, but still has no peer socket
        let want_addr = A::new(A::LOCAL_ADDR, 200);
        assert_eq!(
            alice.run_future(alice_ctl.get_sock_name()).await.unwrap(),
            (0, want_addr.as_bytes().to_vec())
        );
        assert_eq!(
            alice.run_future(alice_ctl.get_peer_name()).await.unwrap(),
            (libc::ENOTCONN as i16, None.into_iter().collect::<Vec<u8>>())
        );

        // Setup Bob as a client, bound to REMOTE_ADDR:300
        println!("Configuring bob...");
        let bob = t.get(1);
        let (bob_ctl, bob_sock) = get_socket_data_plane::<A>(bob).await;
        let sockaddr = A::create(A::REMOTE_ADDR, 300);
        assert_eq!(
            bob.run_future(bob_ctl.bind(&mut sockaddr.into_iter())).await.unwrap() as i32,
            0
        );

        // Verify that Bob is listening on the local socket, but has no peer socket
        let want_addr = A::new(A::REMOTE_ADDR, 300);
        assert_eq!(
            bob.run_future(bob_ctl.get_sock_name()).await.unwrap(),
            (0, want_addr.as_bytes().to_vec())
        );
        assert_eq!(
            bob.run_future(bob_ctl.get_peer_name()).await.unwrap(),
            (libc::ENOTCONN as i16, None.into_iter().collect::<Vec<u8>>())
        );

        // Connect Bob to Alice on LOCAL_ADDR:200
        println!("Connecting bob to alice...");
        let sockaddr = A::create(A::LOCAL_ADDR, 200);
        assert_eq!(
            bob.run_future(bob_ctl.connect(&mut sockaddr.into_iter())).await.unwrap() as i32,
            0
        );

        // Verify that Bob has the peer socket set correctly
        let want_addr = A::new(A::LOCAL_ADDR, 200);
        assert_eq!(
            bob.run_future(bob_ctl.get_peer_name()).await.unwrap(),
            (0, want_addr.as_bytes().to_vec())
        );

        // Send datagram from Bob's socket.
        println!("Writing datagram to bob");
        let body = "Hello".as_ref();
        let hello = make_unspecified_remote_datagram::<A>(body);
        bob_sock.write(&hello[..]).expect("write to bob succeeds");
        // Wait for datagram to arrive on Alice's socket:
        let fut = fasync::OnSignals::new(&alice_sock, zx::Signals::SOCKET_READABLE);
        println!("Waiting for signals");
        t.run_until(fut).await.expect("can run stacks").expect("waiting for readable succeeds");
        let mut data = [0; 2048];
        let rd = alice_sock.read(&mut data[..]).expect("can read from alice");
        let data = &data[..rd];
        let expected = make_datagram::<A>(A::REMOTE_ADDR, 300, body);
        assert_eq!(data, &expected[..]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_hello_v4() {
        test_udp_hello::<SockAddr4>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_hello_v6() {
        test_udp_hello::<SockAddr6>().await;
    }
}
