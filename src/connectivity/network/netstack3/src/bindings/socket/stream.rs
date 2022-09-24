// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Stream sockets, primarily TCP sockets.

use std::{
    convert::Infallible as Never,
    num::{NonZeroU16, NonZeroUsize},
    ops::{ControlFlow, DerefMut as _},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Weak,
    },
};

use assert_matches::assert_matches;
use async_utils::stream::OneOrMany;
use fidl::{
    endpoints::{ClientEnd, ControlHandle as _, RequestStream as _},
    HandleBased as _,
};
use fidl_fuchsia_io as fio;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_posix as fposix;
use fidl_fuchsia_posix_socket as fposix_socket;
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, Peered as _};
use futures::{stream::FusedStream, task::AtomicWaker, Stream, StreamExt as _};
use net_types::{
    ip::{IpAddress, IpVersion, IpVersionMarker, Ipv4, Ipv6},
    SpecifiedAddr, ZonedAddr,
};

use crate::bindings::{
    socket::{IntoErrno, IpSockAddrExt, SockAddr, ZXSIO_SIGNAL_CONNECTED, ZXSIO_SIGNAL_INCOMING},
    util::{IntoFidl as _, TryIntoFidl},
    LockableContext,
};

use netstack3_core::{
    ip::IpExt,
    transport::tcp::{
        buffer::{Buffer, IntoBuffers, ReceiveBuffer, RingBuffer, SendBuffer, SendPayload},
        segment::Payload,
        socket::{
            accept_v4, accept_v6, bind, connect_bound, connect_unbound, create_socket,
            get_bound_v4_info, get_bound_v6_info, get_connection_v4_info, get_connection_v6_info,
            get_listener_v4_info, get_listener_v6_info, listen, AcceptError, BindError, BoundId,
            BoundInfo, ConnectError, ConnectionId, ListenerId, SocketAddr, TcpNonSyncContext,
            UnboundId,
        },
        state::Takeable,
    },
    Ctx,
};

#[derive(Debug)]
enum SocketId {
    Unbound(UnboundId, LocalZirconSocketAndNotifier),
    Bound(BoundId, LocalZirconSocketAndNotifier),
    Connection(ConnectionId),
    Listener(ListenerId),
}

pub(crate) trait SocketWorkerDispatcher:
    TcpNonSyncContext<
    ProvidedBuffers = LocalZirconSocketAndNotifier,
    ReturnedBuffers = PeerZirconSocketAndWatcher,
>
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

/// Local end of a zircon socket pair which will be later provided to state
/// machine inside Core.
#[derive(Debug)]
pub(crate) struct LocalZirconSocketAndNotifier(Arc<zx::Socket>, NeedsDataNotifier);

impl IntoBuffers<ReceiveBufferWithZirconSocket, SendBufferWithZirconSocket>
    for LocalZirconSocketAndNotifier
{
    fn into_buffers(self) -> (ReceiveBufferWithZirconSocket, SendBufferWithZirconSocket) {
        let Self(socket, notifier) = self;
        socket
            .signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_CONNECTED)
            .expect("failed to signal that the connection is established");
        notifier.schedule();
        (
            ReceiveBufferWithZirconSocket::new(Arc::clone(&socket)),
            SendBufferWithZirconSocket::new(socket, notifier),
        )
    }
}

impl Takeable for LocalZirconSocketAndNotifier {
    fn take(&mut self) -> Self {
        let Self(socket, notifier) = self;
        Self(Arc::clone(socket), notifier.clone())
    }
}

/// The peer end of the zircon socket that will later be vended to application,
/// together with objects that are used to receive signals from application.
#[derive(Debug)]
pub(crate) struct PeerZirconSocketAndWatcher {
    peer: zx::Socket,
    watcher: NeedsDataWatcher,
    socket: Arc<zx::Socket>,
}

impl TcpNonSyncContext for crate::bindings::BindingsNonSyncCtxImpl {
    type ReceiveBuffer = ReceiveBufferWithZirconSocket;
    type SendBuffer = SendBufferWithZirconSocket;
    type ReturnedBuffers = PeerZirconSocketAndWatcher;
    type ProvidedBuffers = LocalZirconSocketAndNotifier;

    fn on_new_connection(&mut self, listener: ListenerId) {
        self.tcp_listeners
            .get(listener.into())
            .expect("invalid listener")
            .signal_peer(zx::Signals::NONE, ZXSIO_SIGNAL_INCOMING)
            .expect("failed to signal that the new connection is available");
    }

    fn new_passive_open_buffers() -> (Self::ReceiveBuffer, Self::SendBuffer, Self::ReturnedBuffers)
    {
        let (local, peer) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create sockets");
        let socket = Arc::new(local);
        let notifier = NeedsDataNotifier::new();
        let watcher = notifier.watcher();
        let (rbuf, sbuf) =
            LocalZirconSocketAndNotifier(Arc::clone(&socket), notifier).into_buffers();
        (rbuf, sbuf, PeerZirconSocketAndWatcher { peer, socket, watcher })
    }
}

#[derive(Debug)]
pub(crate) struct ReceiveBufferWithZirconSocket {
    socket: Arc<zx::Socket>,
    capacity: usize,
    out_of_order: RingBuffer,
}

impl ReceiveBufferWithZirconSocket {
    fn new(socket: Arc<zx::Socket>) -> Self {
        let capacity = socket.info().expect("failed to get socket info").tx_buf_max;
        Self { capacity, socket, out_of_order: RingBuffer::default() }
    }
}

impl Takeable for ReceiveBufferWithZirconSocket {
    fn take(&mut self) -> Self {
        core::mem::replace(
            self,
            Self {
                capacity: self.capacity,
                socket: Arc::clone(&self.socket),
                out_of_order: RingBuffer::new(0),
            },
        )
    }
}

impl Buffer for ReceiveBufferWithZirconSocket {
    fn len(&self) -> usize {
        let info = self.socket.info().expect("failed to get socket info");
        info.tx_buf_size
    }

    fn cap(&self) -> usize {
        self.capacity
    }
}

impl From<ReceiveBufferWithZirconSocket> for () {
    fn from(_: ReceiveBufferWithZirconSocket) -> () {
        ()
    }
}

impl ReceiveBuffer for ReceiveBufferWithZirconSocket {
    // We don't need to store anything in our process during passive close: all
    // bytes left that are not yet read by our user will be stored in a zircon
    // socket in the kernel.
    type Residual = ();

    fn write_at<P: Payload>(&mut self, offset: usize, data: &P) -> usize {
        self.out_of_order.write_at(offset, data)
    }

    fn make_readable(&mut self, count: usize) {
        self.out_of_order.make_readable(count);
        let nread = self.out_of_order.read_with(|avail| {
            let mut total = 0;
            for chunk in avail {
                assert_eq!(
                    self.socket.write(*chunk).expect("failed to write into the zircon socket"),
                    chunk.len()
                );
                total += chunk.len();
            }
            total
        });
        assert_eq!(count, nread);
    }
}

#[derive(Debug)]
pub(crate) struct SendBufferWithZirconSocket {
    capacity: usize,
    socket: Arc<zx::Socket>,
    ready_to_send: RingBuffer,
    notifier: NeedsDataNotifier,
}

impl Buffer for SendBufferWithZirconSocket {
    fn len(&self) -> usize {
        let info = self.socket.info().expect("failed to get socket info");
        info.rx_buf_size + self.ready_to_send.len()
    }

    fn cap(&self) -> usize {
        self.capacity
    }
}

impl Takeable for SendBufferWithZirconSocket {
    fn take(&mut self) -> Self {
        let Self { capacity, socket, ready_to_send: data, notifier } = self;
        Self {
            capacity: *capacity,
            socket: Arc::clone(socket),
            ready_to_send: std::mem::replace(data, RingBuffer::new(0)),
            notifier: notifier.clone(),
        }
    }
}

impl SendBufferWithZirconSocket {
    fn new(socket: Arc<zx::Socket>, notifier: NeedsDataNotifier) -> Self {
        let ready_to_send = RingBuffer::default();
        let info = socket.info().expect("failed to get socket info");
        let capacity = info.rx_buf_max + ready_to_send.cap();
        Self { capacity, socket, ready_to_send, notifier }
    }

    fn poll(&mut self) {
        let want_bytes = self.ready_to_send.cap() - self.ready_to_send.len();
        if want_bytes == 0 {
            return;
        }
        let write_result =
            self.ready_to_send.writable_regions().into_iter().try_fold(0, |acc, b| {
                match self.socket.read(b) {
                    Ok(n) => {
                        if n == b.len() {
                            ControlFlow::Continue(acc + n)
                        } else {
                            ControlFlow::Break(acc + n)
                        }
                    }
                    Err(zx::Status::SHOULD_WAIT | zx::Status::PEER_CLOSED) => {
                        ControlFlow::Break(acc)
                    }
                    Err(e) => panic!("unexpected error: {:?}", e),
                }
            });
        let (ControlFlow::Continue(bytes_written) | ControlFlow::Break(bytes_written)) =
            write_result;

        self.ready_to_send.make_readable(bytes_written);
        if bytes_written < want_bytes {
            debug_assert!(write_result.is_break());
            self.notifier.schedule();
        }
    }
}

/// A signal used between Core and Bindings, whenever Bindings receive a
/// notification by the protocol (Core), it should start monitoring the readable
/// signal on the zircon socket and call into Core to send data.
#[derive(Debug)]
struct NeedsData {
    ready: AtomicBool,
    waker: AtomicWaker,
}

impl NeedsData {
    fn new() -> Self {
        Self { ready: AtomicBool::new(false), waker: AtomicWaker::new() }
    }

    fn poll_ready(&self, cx: &mut std::task::Context<'_>) -> std::task::Poll<()> {
        self.waker.register(cx.waker());
        match self.ready.compare_exchange(true, false, Ordering::AcqRel, Ordering::Relaxed) {
            Ok(_) => std::task::Poll::Ready(()),
            Err(_) => std::task::Poll::Pending,
        }
    }

    fn schedule(&self) {
        self.ready.store(true, Ordering::Release);
        self.waker.wake();
    }
}

/// The notifier side of the underlying signal struct, it is meant to be held
/// by the Core side and schedule signals to be received by the Bindings.
#[derive(Debug, Clone)]
struct NeedsDataNotifier {
    inner: Arc<NeedsData>,
}

impl NeedsDataNotifier {
    fn schedule(&self) {
        self.inner.schedule()
    }

    fn new() -> NeedsDataNotifier {
        Self { inner: Arc::new(NeedsData::new()) }
    }

    fn watcher(&self) -> NeedsDataWatcher {
        NeedsDataWatcher { inner: Arc::downgrade(&self.inner) }
    }
}

/// The receiver side of the underlying signal struct, it is meant to be held
/// by the Bindings side. It is a [`Stream`] of wakeups scheduled by the Core
/// and upon receiving those wakeups, Bindings should poll for available data
/// that has been written by the user.
#[derive(Debug)]
struct NeedsDataWatcher {
    inner: Weak<NeedsData>,
}

impl Stream for NeedsDataWatcher {
    type Item = ();

    fn poll_next(
        self: std::pin::Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
    ) -> std::task::Poll<Option<Self::Item>> {
        let this = self.get_mut();
        match this.inner.upgrade() {
            None => std::task::Poll::Ready(None),
            Some(needs_data) => {
                std::task::Poll::Ready(Some(std::task::ready!(needs_data.poll_ready(cx))))
            }
        }
    }
}

/// The watcher terminates when the underlying has been dropped by Core.
impl FusedStream for NeedsDataWatcher {
    fn is_terminated(&self) -> bool {
        self.inner.strong_count() == 0
    }
}

impl SendBuffer for SendBufferWithZirconSocket {
    fn mark_read(&mut self, count: usize) {
        self.ready_to_send.mark_read(count);
        self.poll()
    }

    fn peek_with<'a, F, R>(&'a mut self, offset: usize, f: F) -> R
    where
        F: FnOnce(SendPayload<'a>) -> R,
    {
        self.poll();
        self.ready_to_send.peek_with(offset, f)
    }
}

struct SocketWorker<I: IpExt, C> {
    id: SocketId,
    ctx: C,
    peer: zx::Socket,
    _marker: IpVersionMarker<I>,
}

impl<I: IpExt, C> SocketWorker<I, C> {
    fn new(id: SocketId, ctx: C, peer: zx::Socket) -> Self {
        Self { id, ctx, peer, _marker: Default::default() }
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
    let (local, peer) = zx::Socket::create(zx::SocketOpts::STREAM)
        .map_err(|_: zx::Status| fposix::Errno::Enobufs)?;
    let socket = Arc::new(local);
    match (domain, proto) {
        (fposix_socket::Domain::Ipv4, fposix_socket::StreamSocketProtocol::Tcp) => {
            let id = {
                let mut guard = ctx.lock().await;
                let Ctx { sync_ctx, non_sync_ctx } = guard.deref_mut();
                SocketId::Unbound(
                    create_socket::<Ipv4, _>(sync_ctx, non_sync_ctx),
                    LocalZirconSocketAndNotifier(Arc::clone(&socket), NeedsDataNotifier::new()),
                )
            };
            let worker = SocketWorker::<Ipv4, C>::new(id, ctx.clone(), peer);
            Ok(worker.spawn(request_stream))
        }
        (fposix_socket::Domain::Ipv6, fposix_socket::StreamSocketProtocol::Tcp) => {
            let id = {
                let mut guard = ctx.lock().await;
                let Ctx { sync_ctx, non_sync_ctx } = guard.deref_mut();
                SocketId::Unbound(
                    create_socket::<Ipv6, _>(sync_ctx, non_sync_ctx),
                    LocalZirconSocketAndNotifier(Arc::clone(&socket), NeedsDataNotifier::new()),
                )
            };
            let worker = SocketWorker::<Ipv6, C>::new(id, ctx.clone(), peer);
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

/// Spawns a task that sends more data from the `socket` each time we observe
/// a wakeup through the `watcher`.
fn spawn_send_task<I: IpExt, C>(
    ctx: C,
    socket: Arc<zx::Socket>,
    watcher: NeedsDataWatcher,
    id: ConnectionId,
) where
    C: LockableContext,
    C: Clone + Send + Sync + 'static,
    C::NonSyncCtx: SocketWorkerDispatcher,
{
    fasync::Task::spawn(async move {
        watcher
            .for_each(|()| async {
                let observed = fasync::OnSignals::new(&*socket, zx::Signals::SOCKET_READABLE)
                    .await
                    .expect("failed to observe signals on zircon socket");
                assert!(observed.contains(zx::Signals::SOCKET_READABLE));
                let mut guard = ctx.lock().await;
                let Ctx { sync_ctx, non_sync_ctx } = guard.deref_mut();
                netstack3_core::transport::tcp::socket::do_send::<I, _>(sync_ctx, non_sync_ctx, id)
                    .unwrap_or_else(|err| {
                        log::error!("failed to send on connection {:?}: {:?}", id, err)
                    });
            })
            .await
    })
    .detach();
}

impl<I: IpSockAddrExt + IpExt, C> SocketWorker<I, C>
where
    C: LockableContext,
    C: Clone + Send + Sync + 'static,
    C::NonSyncCtx: SocketWorkerDispatcher,
{
    fn spawn(mut self, request_stream: fposix_socket::StreamSocketRequestStream) {
        fasync::Task::spawn(async move {
            // Keep a set of futures, one per pollable stream. Each future is a
            // `StreamFuture` and so will resolve into a tuple of the next item
            // in the stream and the rest of the stream.
            let mut futures = OneOrMany::new(request_stream.into_future());
            while let Some((request, request_stream)) = futures.next().await {
                let request = match request {
                    None => continue,
                    Some(Err(e)) => {
                        log::warn!("got {} while processing stream requests", e);
                        continue;
                    }
                    Some(Ok(request)) => request,
                };

                match self.handle_request(request).await {
                    ControlFlow::Continue(None) => {}
                    ControlFlow::Break(()) => {
                        request_stream.control_handle().shutdown();
                    }
                    ControlFlow::Continue(Some(new_request_stream)) => {
                        futures.push(new_request_stream.into_future())
                    }
                }
                // `request_stream` received above is the tail of the stream,
                // which might have more requests. Stick it back into the
                // pending future set so we can receive and service those
                // requests. If the stream is exhausted or cancelled due to a
                // `Shutdown` response, it will be dropped internally by the
                // `FuturesUnordered`.
                futures.push(request_stream.into_future())
            }

            // TODO(https://fxbug.dev/103979): Remove socket from core.
            match self.id {
                SocketId::Unbound(_, _) | SocketId::Bound(_, _) | SocketId::Connection(_) => {}
                SocketId::Listener(listener) => {
                    self.ctx.lock().await.non_sync_ctx.unregister_listener(listener)
                }
            }
        })
        .detach()
    }

    async fn bind(&mut self, addr: fnet::SocketAddress) -> Result<(), fposix::Errno> {
        match self.id {
            SocketId::Unbound(unbound, ref mut local_socket) => {
                let addr = I::SocketAddress::from_sock_addr(addr)?;
                let mut guard = self.ctx.lock().await;
                let Ctx { sync_ctx, non_sync_ctx } = guard.deref_mut();
                let bound = bind::<I, _>(
                    sync_ctx,
                    non_sync_ctx,
                    unbound,
                    addr.addr(),
                    NonZeroU16::new(addr.port()),
                )
                .map_err(IntoErrno::into_errno)?;
                self.id = SocketId::Bound(bound, local_socket.take());
                Ok(())
            }
            SocketId::Bound(_, _) | SocketId::Connection(_) | SocketId::Listener(_) => {
                Err(fposix::Errno::Einval)
            }
        }
    }

    async fn connect(&mut self, addr: fnet::SocketAddress) -> Result<(), fposix::Errno> {
        let addr = I::SocketAddress::from_sock_addr(addr)?;
        let ip = SpecifiedAddr::new(addr.addr()).ok_or(fposix::Errno::Einval)?;
        let port = NonZeroU16::new(addr.port()).ok_or(fposix::Errno::Einval)?;
        let mut guard = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = guard.deref_mut();
        let (connection, socket, watcher) = match self.id {
            SocketId::Bound(bound, LocalZirconSocketAndNotifier(ref socket, ref notifier)) => {
                let connection = connect_bound::<I, _>(
                    sync_ctx,
                    non_sync_ctx,
                    bound,
                    SocketAddr { ip, port },
                    LocalZirconSocketAndNotifier(Arc::clone(socket), notifier.clone()),
                )
                .map_err(IntoErrno::into_errno)?;
                Ok((connection, Arc::clone(socket), notifier.watcher()))
            }
            SocketId::Unbound(unbound, LocalZirconSocketAndNotifier(ref socket, ref notifier)) => {
                let connected = connect_unbound::<I, _>(
                    sync_ctx,
                    non_sync_ctx,
                    unbound,
                    SocketAddr { ip, port },
                    LocalZirconSocketAndNotifier(Arc::clone(socket), notifier.clone()),
                )
                .map_err(IntoErrno::into_errno)?;
                Ok((connected, Arc::clone(socket), notifier.watcher()))
            }
            SocketId::Listener(_) => Err(fposix::Errno::Einval),
            SocketId::Connection(_) => Err(fposix::Errno::Eisconn),
        }?;
        spawn_send_task::<I, _>(self.ctx.clone(), socket, watcher, connection);
        self.id = SocketId::Connection(connection);
        Ok(())
    }

    async fn listen(&mut self, backlog: i16) -> Result<(), fposix::Errno> {
        match self.id {
            SocketId::Bound(bound, ref mut local_socket) => {
                let mut guard = self.ctx.lock().await;
                let Ctx { sync_ctx, non_sync_ctx } = guard.deref_mut();
                let backlog = NonZeroUsize::new(backlog as usize).ok_or(fposix::Errno::Einval)?;
                let listener = listen::<I, _>(sync_ctx, non_sync_ctx, bound, backlog);
                let LocalZirconSocketAndNotifier(local, _) = local_socket.take();
                self.id = SocketId::Listener(listener);
                non_sync_ctx.register_listener(
                    listener,
                    Arc::try_unwrap(local)
                        .expect("the local end of the socket should never be shared"),
                );
                Ok(())
            }
            SocketId::Unbound(_, _) | SocketId::Connection(_) | SocketId::Listener(_) => {
                Err(fposix::Errno::Einval)
            }
        }
    }

    async fn get_sock_name(&self) -> Result<fnet::SocketAddress, fposix::Errno> {
        let mut guard = self.ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx: _ } = guard.deref_mut();
        match self.id {
            SocketId::Unbound(_, _) => Err(fposix::Errno::Einval),
            SocketId::Bound(id, _) => Ok({
                match I::VERSION {
                    IpVersion::V4 => {
                        let BoundInfo { addr, port } = get_bound_v4_info(sync_ctx, id);
                        (addr, port).into_fidl().into_sock_addr()
                    }
                    IpVersion::V6 => {
                        let BoundInfo { addr, port } = get_bound_v6_info(sync_ctx, id);
                        (addr, port).into_fidl().into_sock_addr()
                    }
                }
            }),
            SocketId::Listener(id) => Ok({
                match I::VERSION {
                    IpVersion::V4 => {
                        let BoundInfo { addr, port } = get_listener_v4_info(sync_ctx, id);
                        (addr, port).into_fidl().into_sock_addr()
                    }
                    IpVersion::V6 => {
                        let BoundInfo { addr, port } = get_listener_v6_info(sync_ctx, id);
                        (addr, port).into_fidl().into_sock_addr()
                    }
                }
            }),
            SocketId::Connection(id) => Ok({
                match I::VERSION {
                    IpVersion::V4 => {
                        get_connection_v4_info(sync_ctx, id).local_addr.into_fidl().into_sock_addr()
                    }
                    IpVersion::V6 => {
                        get_connection_v6_info(sync_ctx, id).local_addr.into_fidl().into_sock_addr()
                    }
                }
            }),
        }
    }

    async fn accept(
        &mut self,
        want_addr: bool,
    ) -> Result<
        (Option<Box<fnet::SocketAddress>>, ClientEnd<fposix_socket::StreamSocketMarker>),
        fposix::Errno,
    > {
        match self.id {
            SocketId::Listener(listener) => {
                let mut guard = self.ctx.lock().await;
                let Ctx { sync_ctx, non_sync_ctx } = guard.deref_mut();
                let (accepted, addr, PeerZirconSocketAndWatcher { peer, watcher, socket }) =
                    match I::VERSION {
                        IpVersion::V4 => {
                            let (accepted, SocketAddr { ip, port }, peer) =
                                accept_v4(sync_ctx, non_sync_ctx, listener)
                                    .map_err(IntoErrno::into_errno)?;
                            (
                                accepted,
                                fnet::Ipv4SocketAddress::new(
                                    Some(ZonedAddr::Unzoned(ip)),
                                    port.get(),
                                )
                                .into_sock_addr(),
                                peer,
                            )
                        }
                        IpVersion::V6 => {
                            let (accepted, SocketAddr { ip, port }, peer) =
                                accept_v6(sync_ctx, non_sync_ctx, listener)
                                    .map_err(IntoErrno::into_errno)?;
                            (
                                accepted,
                                fnet::Ipv6SocketAddress::new(
                                    Some(ZonedAddr::Unzoned(ip)),
                                    port.get(),
                                )
                                .into_sock_addr(),
                                peer,
                            )
                        }
                    };
                let (client, request_stream) =
                    fidl::endpoints::create_request_stream::<fposix_socket::StreamSocketMarker>()
                        .expect("failed to create new fidl endpoints");
                spawn_send_task::<I, _>(self.ctx.clone(), socket, watcher, accepted);
                let worker = SocketWorker::<I, C>::new(
                    SocketId::Connection(accepted),
                    self.ctx.clone(),
                    peer,
                );
                worker.spawn(request_stream);
                Ok((want_addr.then(|| Box::new(addr)), client))
            }
            SocketId::Unbound(_, _) | SocketId::Connection(_) | SocketId::Bound(_, _) => {
                Err(fposix::Errno::Einval)
            }
        }
    }

    /// Returns a [`ControlFlow`] to indicate whether the parent stream should
    /// continue being polled or dropped.
    ///
    /// If `Some(stream)` is returned in the `Continue` case, `stream` is a new
    /// stream of events that should be polled concurrently with the parent
    /// stream.
    async fn handle_request(
        &mut self,
        request: fposix_socket::StreamSocketRequest,
    ) -> ControlFlow<(), Option<fposix_socket::StreamSocketRequestStream>> {
        match request {
            fposix_socket::StreamSocketRequest::Bind { addr, responder } => {
                responder_send!(responder, &mut self.bind(addr).await);
            }
            fposix_socket::StreamSocketRequest::Connect { addr, responder } => {
                responder_send!(responder, &mut self.connect(addr).await);
            }
            fposix_socket::StreamSocketRequest::DescribeDeprecated { responder } => {
                let socket = self
                    .peer
                    .duplicate_handle(zx::Rights::SAME_RIGHTS)
                    .expect("failed to duplicate the socket handle");
                responder_send!(
                    responder,
                    &mut fio::NodeInfoDeprecated::StreamSocket(fio::StreamSocket { socket })
                );
            }
            fposix_socket::StreamSocketRequest::Describe2 { responder } => {
                let socket = self
                    .peer
                    .duplicate_handle(zx::Rights::SAME_RIGHTS)
                    .expect("failed to duplicate the socket handle");
                responder_send!(
                    responder,
                    fposix_socket::StreamSocketDescribe2Response {
                        socket: Some(socket),
                        ..fposix_socket::StreamSocketDescribe2Response::EMPTY
                    }
                );
            }
            fposix_socket::StreamSocketRequest::Listen { backlog, responder } => {
                responder_send!(responder, &mut self.listen(backlog).await);
            }
            fposix_socket::StreamSocketRequest::Accept { want_addr, responder } => {
                responder_send!(responder, &mut self.accept(want_addr).await);
            }
            fposix_socket::StreamSocketRequest::Reopen {
                rights_request,
                object_request: _,
                control_handle: _,
            } => {
                todo!("https://fxbug.dev/77623: rights_request={:?}", rights_request);
            }
            fposix_socket::StreamSocketRequest::Close { responder } => {
                responder_send!(responder, &mut Ok(()));
                return ControlFlow::Break(());
            }
            fposix_socket::StreamSocketRequest::GetConnectionInfo { responder: _ } => {
                todo!("https://fxbug.dev/77623");
            }
            fposix_socket::StreamSocketRequest::GetAttributes { query, responder: _ } => {
                todo!("https://fxbug.dev/77623: query={:?}", query);
            }
            fposix_socket::StreamSocketRequest::UpdateAttributes { payload, responder: _ } => {
                todo!("https://fxbug.dev/77623: attributes={:?}", payload);
            }
            fposix_socket::StreamSocketRequest::Sync { responder } => {
                responder_send!(responder, &mut Err(zx::Status::NOT_SUPPORTED.into_raw()));
            }
            fposix_socket::StreamSocketRequest::Clone { flags: _, object, control_handle: _ } => {
                let channel = fidl::AsyncChannel::from_channel(object.into_channel())
                    .expect("failed to create async channel");
                let events = fposix_socket::StreamSocketRequestStream::from_channel(channel);
                return ControlFlow::Continue(Some(events));
            }
            fposix_socket::StreamSocketRequest::Clone2 { request, control_handle: _ } => {
                let channel = fidl::AsyncChannel::from_channel(request.into_channel())
                    .expect("failed to create async channel");
                let events = fposix_socket::StreamSocketRequestStream::from_channel(channel);
                return ControlFlow::Continue(Some(events));
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
            fposix_socket::StreamSocketRequest::SetAttr { flags: _, attributes: _, responder } => {
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
            fposix_socket::StreamSocketRequest::Query { responder } => {
                responder_send!(responder, fposix_socket::STREAM_SOCKET_PROTOCOL_NAME.as_bytes());
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
            fposix_socket::StreamSocketRequest::SetSendBuffer { value_bytes: _, responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::GetSendBuffer { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::SetReceiveBuffer { value_bytes: _, responder } => {
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
            fposix_socket::StreamSocketRequest::SetOutOfBandInline { value: _, responder } => {
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
            fposix_socket::StreamSocketRequest::SetTimestampDeprecated { value: _, responder } => {
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
                responder_send!(responder, &mut self.get_sock_name().await);
            }
            fposix_socket::StreamSocketRequest::GetPeerName { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::Shutdown { mode: _, responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::SetIpTypeOfService { value: _, responder } => {
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
            fposix_socket::StreamSocketRequest::SetIpMulticastTtl { value: _, responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::GetIpMulticastTtl { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::SetIpMulticastLoopback { value: _, responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::GetIpMulticastLoopback { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::AddIpMembership { membership: _, responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::DropIpMembership { membership: _, responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::AddIpv6Membership { membership: _, responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::DropIpv6Membership { membership: _, responder } => {
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
            fposix_socket::StreamSocketRequest::SetIpv6UnicastHops { value: _, responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::GetIpv6UnicastHops { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::SetIpv6ReceiveHopLimit { value: _, responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::GetIpv6ReceiveHopLimit { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::SetIpv6MulticastHops { value: _, responder } => {
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
            fposix_socket::StreamSocketRequest::GetIpv6ReceiveTrafficClass { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::SetIpv6TrafficClass { value: _, responder } => {
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
            fposix_socket::StreamSocketRequest::SetTcpMaxSegment { value_bytes: _, responder } => {
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
            fposix_socket::StreamSocketRequest::SetTcpKeepAliveCount { value: _, responder } => {
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
            fposix_socket::StreamSocketRequest::SetTcpLinger { value_secs: _, responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::GetTcpLinger { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::SetTcpDeferAccept { value_secs: _, responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::GetTcpDeferAccept { responder } => {
                responder_send!(responder, &mut Err(fposix::Errno::Eopnotsupp));
            }
            fposix_socket::StreamSocketRequest::SetTcpWindowClamp { value: _, responder } => {
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
            fposix_socket::StreamSocketRequest::SetTcpCongestion { value: _, responder } => {
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
        ControlFlow::Continue(None)
    }
}

impl<A: IpAddress> TryIntoFidl<<A::Version as IpSockAddrExt>::SocketAddress> for SocketAddr<A>
where
    A::Version: IpSockAddrExt,
{
    type Error = Never;

    fn try_into_fidl(self) -> Result<<A::Version as IpSockAddrExt>::SocketAddress, Self::Error> {
        let Self { ip, port } = self;
        Ok((Some(ip), port).into_fidl())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_BYTES: &'static [u8] = b"Hello";

    #[test]
    fn receive_buffer() {
        let (local, peer) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create zircon socket");
        let mut rbuf = ReceiveBufferWithZirconSocket::new(Arc::new(local));
        assert_eq!(rbuf.write_at(0, &TEST_BYTES), TEST_BYTES.len());
        assert_eq!(rbuf.write_at(TEST_BYTES.len() * 2, &TEST_BYTES), TEST_BYTES.len());
        assert_eq!(rbuf.write_at(TEST_BYTES.len(), &TEST_BYTES), TEST_BYTES.len());
        rbuf.make_readable(TEST_BYTES.len() * 3);
        let mut buf = [0u8; TEST_BYTES.len() * 3];
        assert_eq!(rbuf.len(), TEST_BYTES.len() * 3);
        assert_eq!(peer.read(&mut buf), Ok(TEST_BYTES.len() * 3));
        assert_eq!(&buf, b"HelloHelloHello");
    }

    #[test]
    fn send_buffer() {
        let (local, peer) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create zircon socket");
        let notifier = NeedsDataNotifier::new();
        let mut sbuf = SendBufferWithZirconSocket::new(Arc::new(local), notifier);
        assert_eq!(peer.write(TEST_BYTES), Ok(TEST_BYTES.len()));
        assert_eq!(sbuf.len(), TEST_BYTES.len());
        sbuf.peek_with(0, |avail| {
            assert_eq!(avail, SendPayload::Contiguous(TEST_BYTES));
        });
        assert_eq!(peer.write(TEST_BYTES), Ok(TEST_BYTES.len()));
        assert_eq!(sbuf.len(), TEST_BYTES.len() * 2);
        sbuf.mark_read(TEST_BYTES.len());
        assert_eq!(sbuf.len(), TEST_BYTES.len());
        sbuf.peek_with(0, |avail| {
            assert_eq!(avail, SendPayload::Contiguous(TEST_BYTES));
        });
    }
}
