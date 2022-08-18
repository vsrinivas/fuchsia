// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines how TCP state machines are used for TCP sockets.
//!
//! TCP state machine implemented in the parent module aims to only implement
//! RFC 793 which lacks posix semantics.
//!
//! To actually support posix-style sockets:
//! We would need two kinds of active sockets, listeners/connections (or
//! server sockets/client sockets; both are not very accurate terms, the key
//! difference is that the former has only local addresses but the later has
//! remote addresses in addition). [`Connection`]s are backed by a state
//! machine, however the state can be in any state. [`Listener`]s don't have
//! state machines, but they create [`Connection`]s that are backed by
//! [`State::Listen`] an incoming SYN and keep track of whether the connection
//! is established.

pub(crate) mod demux;
mod icmp;
pub(crate) mod isn;

use alloc::{collections::VecDeque, vec::Vec};
use core::{
    convert::Infallible as Never,
    fmt::Debug,
    marker::PhantomData,
    num::{NonZeroU16, NonZeroUsize},
    ops::{DerefMut as _, RangeInclusive},
};
use nonzero_ext::nonzero;

use assert_matches::assert_matches;
use log::warn;
use net_types::{ip::IpAddress, SpecifiedAddr};
use packet::Buf;
use packet_formats::ip::IpProto;
use rand::RngCore;

use crate::{
    algorithm::{PortAlloc, PortAllocImpl},
    context::{EventContext, InstantContext},
    data_structures::{
        id_map::IdMap,
        socketmap::{IterShadows as _, SocketMap, Tagged},
    },
    ip::{
        socket::{IpSock, IpSockCreationError},
        BufferTransportIpContext, IpDeviceId, IpDeviceIdContext, IpExt, TransportIpContext,
    },
    socket::{
        address::{ConnAddr, ConnIpAddr, IpPortSpec, ListenerAddr, ListenerIpAddr},
        AddrVec, Bound, BoundSocketMap, IncompatibleError, InsertError, RemoveResult,
        SocketAddrTypeTag, SocketMapAddrStateSpec, SocketMapConflictPolicy, SocketMapStateSpec,
        SocketTypeState as _, SocketTypeStateMut as _,
    },
    transport::tcp::{
        buffer::{IntoBuffers, ReceiveBuffer, SendBuffer},
        socket::{demux::tcp_serialize_segment, isn::IsnGenerator},
        state::{Closed, Initial, State, Takeable},
    },
    Instant,
};

/// Per RFC 879 section 1 (https://tools.ietf.org/html/rfc879#section-1):
///
/// THE TCP MAXIMUM SEGMENT SIZE IS THE IP MAXIMUM DATAGRAM SIZE MINUS
/// FORTY.
///   The default IP Maximum Datagram Size is 576.
///   The default TCP Maximum Segment Size is 536.
const DEFAULT_MAXIMUM_SEGMENT_SIZE: u32 = 536;

/// Non-sync context for TCP.
///
/// The relationship between buffers defined in the context is as follows:
///
/// The application holds onto the client end buffers and uses them to read or
/// write data. The peer end of those buffers will be held by the state machine
/// inside the netstack. Specialized receive/send buffers will be derived from
/// the netstack end buffers.
///
/// +-------------------------------+
/// |       +--------------+        |
/// |       |  client end  |        |
/// |       |    buffers   |        |
/// |       +------+-------+        |
/// |              |     application|
/// +--------------+----------------+
///                |
/// +--------------+----------------+
/// |              |        netstack|
/// |   +---+------+-------+---+    |
/// |   |   | netstack end |   |    |
/// |   | +-+-  buffers   -+-+ |    |
/// |   +-+-+--------------+-+-+    |
/// |     v                  v      |
/// |receive buffer     send buffer |
/// +-------------------------------+
pub trait TcpNonSyncContext: InstantContext {
    /// Receive buffer used by TCP.
    type ReceiveBuffer: ReceiveBuffer;
    /// Send buffer used by TCP.
    type SendBuffer: SendBuffer;
    /// The object that will be returned by the state machine when a passive
    /// open connection becomes established. The bindings can use this object
    /// to read/write bytes from/into the created buffers.
    type ClientEndBuffers: Debug;
    /// The object that is needed from the bindings to initiate a connection;
    /// it will be later used to construct buffers when the connection becomes
    /// established.
    type NetstackEndBuffers: Debug + Takeable + IntoBuffers<Self::ReceiveBuffer, Self::SendBuffer>;

    /// A new connection is ready to be accepted on the listener.
    fn on_new_connection(&mut self, listener: ListenerId);
    /// Creates new buffers and returns the object that Bindings need to
    /// read/write from/into the created buffers.
    fn new_passive_open_buffers() -> (Self::ReceiveBuffer, Self::SendBuffer, Self::ClientEndBuffers);
}

/// Sync context for TCP.
pub trait TcpSyncContext<I: IpExt, C: TcpNonSyncContext>: IpDeviceIdContext<I> {
    /// Calls the function with an immutable reference to an initial sequence
    /// number generator and a mutable reference to TCP socket state.
    fn with_isn_generator_and_tcp_sockets_mut<
        O,
        F: FnOnce(&IsnGenerator<C::Instant>, &mut TcpSockets<I, Self::DeviceId, C>) -> O,
    >(
        &mut self,
        cb: F,
    ) -> O;

    /// Calls the function with a mutable reference to TCP socket state.
    fn with_tcp_sockets_mut<O, F: FnOnce(&mut TcpSockets<I, Self::DeviceId, C>) -> O>(
        &mut self,
        cb: F,
    ) -> O {
        self.with_isn_generator_and_tcp_sockets_mut(|_isn, sockets| cb(sockets))
    }
}

/// Socket address includes the ip address and the port number.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct SocketAddr<A: IpAddress> {
    /// The IP component of the address.
    pub ip: SpecifiedAddr<A>,
    /// The port component of the address.
    pub port: NonZeroU16,
}

/// An implementation of [`IpTransportContext`] for TCP.
pub(crate) enum TcpIpTransportContext {}

/// Uninstantiatable type for implementing [`SocketMapStateSpec`].
struct TcpSocketSpec<Ip, Device, NonSyncContext>(PhantomData<(Ip, Device, NonSyncContext)>, Never);

impl<I: IpExt, D: IpDeviceId, C: TcpNonSyncContext> SocketMapStateSpec for TcpSocketSpec<I, D, C> {
    type ListenerId = MaybeListenerId;
    type ConnId = ConnectionId;

    type ListenerState = MaybeListener<C::ClientEndBuffers>;
    type ConnState =
        Connection<I, D, C::Instant, C::ReceiveBuffer, C::SendBuffer, C::NetstackEndBuffers>;

    type ListenerSharingState = ();
    type ConnSharingState = ();
    type AddrVecTag = SocketAddrTypeTag<()>;

    type ListenerAddrState = MaybeListenerId;
    type ConnAddrState = ConnectionId;
}

impl<I: IpExt, D: IpDeviceId, C: TcpNonSyncContext>
    SocketMapConflictPolicy<ListenerAddr<I::Addr, D, NonZeroU16>, (), IpPortSpec<I, D>>
    for TcpSocketSpec<I, D, C>
{
    fn check_for_conflicts(
        (): &(),
        addr: &ListenerAddr<I::Addr, D, NonZeroU16>,
        socketmap: &SocketMap<AddrVec<IpPortSpec<I, D>>, Bound<Self>>,
    ) -> Result<(), InsertError> {
        let addr = AddrVec::Listen(*addr);
        // Check if any shadow address is present, specifically, if
        // there is an any-listener with the same port.
        if addr.iter_shadows().any(|a| socketmap.get(&a).is_some()) {
            return Err(InsertError::ShadowAddrExists);
        }

        // Check if shadower exists. Note: Listeners do conflict
        // with existing connections.
        if socketmap.descendant_counts(&addr).len() > 0 {
            return Err(InsertError::ShadowerExists);
        }
        Ok(())
    }
}

impl<I: IpExt, D: IpDeviceId, C: TcpNonSyncContext>
    SocketMapConflictPolicy<ConnAddr<I::Addr, D, NonZeroU16, NonZeroU16>, (), IpPortSpec<I, D>>
    for TcpSocketSpec<I, D, C>
{
    fn check_for_conflicts(
        (): &(),
        _addr: &ConnAddr<I::Addr, D, NonZeroU16, NonZeroU16>,
        _socketmap: &SocketMap<AddrVec<IpPortSpec<I, D>>, Bound<Self>>,
    ) -> Result<(), InsertError> {
        // Connections don't conflict with existing listeners. If there
        // are connections with the same local and remote address, it
        // will be decided by the socket sharing options.
        Ok(())
    }
}

impl<A: IpAddress, D, LI> Tagged<ListenerAddr<A, D, LI>> for MaybeListenerId {
    type Tag = SocketAddrTypeTag<()>;
    fn tag(&self, address: &ListenerAddr<A, D, LI>) -> Self::Tag {
        (address, ()).into()
    }
}

impl<A: IpAddress, D, LI, RI> Tagged<ConnAddr<A, D, LI, RI>> for ConnectionId {
    type Tag = SocketAddrTypeTag<()>;
    fn tag(&self, address: &ConnAddr<A, D, LI, RI>) -> Self::Tag {
        (address, ()).into()
    }
}

/// Inactive sockets which are created but do not participate in demultiplexing
/// incoming segments.
#[derive(Debug, Clone, PartialEq)]
struct Inactive;

/// Holds all the TCP socket states.
pub struct TcpSockets<I: IpExt, D: IpDeviceId, C: TcpNonSyncContext> {
    port_alloc: PortAlloc<BoundSocketMap<IpPortSpec<I, D>, TcpSocketSpec<I, D, C>>>,
    inactive: IdMap<Inactive>,
    socketmap: BoundSocketMap<IpPortSpec<I, D>, TcpSocketSpec<I, D, C>>,
}

impl<I: IpExt, D: IpDeviceId, C: TcpNonSyncContext> PortAllocImpl
    for BoundSocketMap<IpPortSpec<I, D>, TcpSocketSpec<I, D, C>>
{
    const TABLE_SIZE: NonZeroUsize = nonzero!(20usize);
    const EPHEMERAL_RANGE: RangeInclusive<u16> = 49152..=65535;
    type Id = I::Addr;

    fn is_port_available(&self, addr: &I::Addr, port: u16) -> bool {
        // We can safely unwrap here, because the ports received in
        // `is_port_available` are guaranteed to be in `EPHEMERAL_RANGE`.
        let port = NonZeroU16::new(port).unwrap();
        let root_addr = AddrVec::from(ListenerAddr {
            ip: ListenerIpAddr { addr: SpecifiedAddr::new(*addr), identifier: port },
            device: None,
        });

        // A port is free if there are no sockets currently using it, and if
        // there are no sockets that are shadowing it.
        root_addr.iter_shadows().all(|a| match &a {
            AddrVec::Listen(l) => self.listeners().get_by_addr(&l).is_none(),
            AddrVec::Conn(_c) => {
                unreachable!("no connection shall be included in an iteration from a listener")
            }
        }) && self.get_shadower_counts(&root_addr) == 0
    }
}

impl<I: IpExt, D: IpDeviceId, C: TcpNonSyncContext> TcpSockets<I, D, C> {
    fn get_listener_by_id_mut(
        &mut self,
        id: ListenerId,
    ) -> Option<&mut Listener<C::ClientEndBuffers>> {
        self.socketmap.listeners_mut().get_by_id_mut(&MaybeListenerId::from(id)).map(
            |(maybe_listener, _sharing, _local_addr)| match maybe_listener {
                MaybeListener::Bound(_) => {
                    unreachable!("contract violated: ListenerId points to an inactive entry")
                }
                MaybeListener::Listener(l) => l,
            },
        )
    }

    pub(crate) fn new(rng: &mut impl RngCore) -> Self {
        Self {
            port_alloc: PortAlloc::new(rng),
            inactive: IdMap::new(),
            socketmap: Default::default(),
        }
    }
}

/// A link stored in each passively created connections that points back to the
/// parent listener.
///
/// The link is an [`Acceptor::Pending`] iff the acceptee is in the pending
/// state; The link is an [`Acceptor::Ready`] iff the acceptee is ready and has
/// an established connection.
#[derive(Debug)]
enum Acceptor {
    Pending(ListenerId),
    Ready(ListenerId),
}

/// The Connection state.
///
/// Note: the `state` is not guaranteed to be [`State::Established`]. The
/// connection can be in any state as long as both the local and remote socket
/// addresses are specified.
#[derive(Debug)]
struct Connection<I: IpExt, D: IpDeviceId, II: Instant, R: ReceiveBuffer, S: SendBuffer, ActiveOpen>
{
    acceptor: Option<Acceptor>,
    state: State<II, R, S, ActiveOpen>,
    ip_sock: IpSock<I, D>,
}

/// The Listener state.
///
/// State for sockets that participate in the passive open. Contrary to
/// [`Connection`], only the local address is specified.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
struct Listener<PassiveOpen> {
    backlog: NonZeroUsize,
    ready: VecDeque<(ConnectionId, PassiveOpen)>,
    pending: Vec<ConnectionId>,
    // If ip sockets can be half-specified so that only the local address
    // is needed, we can construct an ip socket here to be reused.
}

impl<PassiveOpen> Listener<PassiveOpen> {
    fn new(backlog: NonZeroUsize) -> Self {
        Self { backlog, ready: VecDeque::new(), pending: Vec::new() }
    }
}

/// Represents either a bound socket or a listener socket.
#[derive(Debug)]
enum MaybeListener<PassiveOpen> {
    Bound(Inactive),
    Listener(Listener<PassiveOpen>),
}

// TODO(https://fxbug.dev/38297): The following IDs are all `Clone + Copy`,
// which makes it possible for the client to keep them for longer than they are
// valid and cause panics. Find a way to make it harder to misuse.
/// The ID to an unbound socket.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct UnboundId(usize);
/// The ID to a bound socket.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BoundId(usize);
/// The ID to a listener socket.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ListenerId(usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct MaybeListenerId(usize);
/// The ID to a connection socket.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ConnectionId(usize);

impl SocketMapAddrStateSpec for MaybeListenerId {
    type Id = MaybeListenerId;
    type SharingState = ();

    fn new((): &(), id: MaybeListenerId) -> Self {
        id
    }

    fn remove_by_id(&mut self, id: MaybeListenerId) -> RemoveResult {
        assert_eq!(self, &id);
        RemoveResult::IsLast
    }

    fn try_get_dest<'a, 'b>(
        &'b mut self,
        (): &'a (),
    ) -> Result<&'b mut Vec<MaybeListenerId>, IncompatibleError> {
        Err(IncompatibleError)
    }
}

impl SocketMapAddrStateSpec for ConnectionId {
    type Id = ConnectionId;
    type SharingState = ();

    fn new((): &(), id: ConnectionId) -> Self {
        id
    }

    fn remove_by_id(&mut self, id: ConnectionId) -> RemoveResult {
        assert_eq!(self, &id);
        RemoveResult::IsLast
    }

    fn try_get_dest<'a, 'b>(
        &'b mut self,
        (): &'a (),
    ) -> Result<&'b mut Vec<ConnectionId>, IncompatibleError> {
        Err(IncompatibleError)
    }
}

/// Creates a new socket in unbound state.
pub fn create_socket<I, SC, C>(sync_ctx: &mut SC, _ctx: &mut C) -> UnboundId
where
    I: IpExt,
    C: TcpNonSyncContext,
    SC: TcpSyncContext<I, C>,
{
    UnboundId(sync_ctx.with_tcp_sockets_mut(|sockets| sockets.inactive.push(Inactive)))
}

/// Possible errors for the bind operation.
#[derive(Debug)]
pub enum BindError {
    /// The local address does not belong to us.
    NoLocalAddr,
    /// Cannot allocate a port for the local address.
    NoPort,
    /// The address that is intended to be bound on is already in use.
    Conflict,
}

/// Binds an unbound socket to a local socket address.
pub fn bind<I, SC, C>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    id: UnboundId,
    local_ip: I::Addr,
    port: Option<NonZeroU16>,
) -> Result<BoundId, BindError>
where
    I: IpExt,
    C: TcpNonSyncContext,
    SC: TcpSyncContext<I, C> + TransportIpContext<I, C>,
{
    // TODO(https://fxbug.dev/104300): Check if local_ip is a unicast address.
    let port = match port {
        None => {
            sync_ctx.with_tcp_sockets_mut(|TcpSockets { port_alloc, inactive: _, socketmap }| {
                match port_alloc.try_alloc(&local_ip, &socketmap) {
                    Some(port) => {
                        Ok(NonZeroU16::new(port).expect("ephemeral ports must be non-zero"))
                    }
                    None => Err(BindError::NoPort),
                }
            })?
        }
        Some(port) => port,
    };
    bind_inner(sync_ctx, ctx, id, local_ip, port)
}

fn bind_inner<I, SC, C>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    id: UnboundId,
    local_ip: I::Addr,
    port: NonZeroU16,
) -> Result<BoundId, BindError>
where
    I: IpExt,
    C: TcpNonSyncContext,
    SC: TcpSyncContext<I, C> + TransportIpContext<I, C>,
{
    let idmap_key = id.into();
    let inactive = sync_ctx
        .with_tcp_sockets_mut(|sockets| sockets.inactive.remove(idmap_key))
        .expect("invalid unbound socket id");
    let local_ip = SpecifiedAddr::new(local_ip);
    if let Some(ip) = local_ip {
        if sync_ctx.get_device_with_assigned_addr(ip).is_none() {
            return Err(BindError::NoLocalAddr);
        }
    }
    sync_ctx
        .with_tcp_sockets_mut(|sockets| {
            sockets.socketmap.listeners_mut().try_insert(
                ListenerAddr {
                    ip: ListenerIpAddr { addr: local_ip, identifier: port },
                    device: None,
                },
                MaybeListener::Bound(inactive.clone()),
                // TODO(https://fxbug.dev/101596): Support sharing for TCP sockets.
                (),
            )
        })
        .map(|MaybeListenerId(x)| BoundId(x))
        .map_err(|_: (InsertError, MaybeListener<_>, ())| {
            assert_eq!(
                sync_ctx
                    .with_tcp_sockets_mut(|sockets| sockets.inactive.insert(idmap_key, inactive)),
                None
            );
            BindError::Conflict
        })
}

/// Listens on an already bound socket.
pub fn listen<I, SC, C>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    id: BoundId,
    backlog: NonZeroUsize,
) -> ListenerId
where
    I: IpExt,
    C: TcpNonSyncContext,
    SC: TcpSyncContext<I, C>,
{
    let id = MaybeListenerId::from(id);
    sync_ctx.with_tcp_sockets_mut(|sockets| {
        let (listener, _, _): (_, &(), &ListenerAddr<_, _, _>) =
            sockets.socketmap.listeners_mut().get_by_id_mut(&id).expect("invalid listener id");

        match listener {
            MaybeListener::Bound(_) => {
                *listener = MaybeListener::Listener(Listener::new(backlog));
            }
            MaybeListener::Listener(_) => {
                unreachable!("invalid bound id that points to a listener entry")
            }
        }
    });

    ListenerId(id.into())
}

/// Possible errors for accept operation.
#[derive(Debug)]
pub enum AcceptError {
    /// There is no established socket currently.
    WouldBlock,
}

/// Accepts an established socket from the queue of a listener socket.
pub fn accept<I, SC, C>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    id: ListenerId,
) -> Result<(ConnectionId, SocketAddr<I::Addr>, C::ClientEndBuffers), AcceptError>
where
    I: IpExt,
    C: TcpNonSyncContext,
    SC: TcpSyncContext<I, C>,
{
    sync_ctx.with_tcp_sockets_mut(|sockets| {
        let listener = sockets.get_listener_by_id_mut(id).expect("invalid listener id");
        let (conn_id, client_buffers) =
            listener.ready.pop_front().ok_or(AcceptError::WouldBlock)?;
        let (conn, _, conn_addr): (_, &(), _) = sockets
            .socketmap
            .conns_mut()
            .get_by_id_mut(&conn_id)
            .expect("failed to retrieve the connection state");
        conn.acceptor = None;
        let (remote_ip, remote_port) = conn_addr.ip.remote;
        Ok((conn_id, SocketAddr { ip: remote_ip, port: remote_port }, client_buffers))
    })
}

/// Possible errors when connecting a socket.
#[derive(Debug)]
pub enum ConnectError {
    /// Cannot allocate a local port for the connection.
    NoPort,
    /// Cannot find a route to the remote host.
    NoRoute,
}

/// Connects a socket that has been bound locally.
pub fn connect_bound<I, SC, C>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    id: BoundId,
    remote: SocketAddr<I::Addr>,
    netstack_buffers: C::NetstackEndBuffers,
) -> Result<ConnectionId, ConnectError>
where
    I: IpExt,
    C: TcpNonSyncContext,
    SC: TcpSyncContext<I, C> + BufferTransportIpContext<I, C, Buf<Vec<u8>>>,
{
    let bound_id = MaybeListenerId::from(id);
    let ListenerAddr { ip: ListenerIpAddr { addr: local_ip, identifier: local_port }, device } =
        sync_ctx.with_tcp_sockets_mut(|sockets| {
            let (bound, (), bound_addr) =
                sockets.socketmap.listeners().get_by_id(&bound_id).expect("invalid socket id");
            assert_matches!(bound, MaybeListener::Bound(_));
            *bound_addr
        });
    let ip_sock = sync_ctx
        .new_ip_socket(ctx, device, local_ip, remote.ip, IpProto::Tcp.into(), None)
        .map_err(|err| match err {
            IpSockCreationError::Route(_) => ConnectError::NoRoute,
        })?;
    let conn_id = connect_inner(sync_ctx, ctx, ip_sock, local_port, remote.port, netstack_buffers)?;
    let _: Option<_> = sync_ctx
        .with_tcp_sockets_mut(|sockets| sockets.socketmap.listeners_mut().remove(&bound_id));
    Ok(conn_id)
}

/// Connects a socket that is in unbound state.
pub fn connect_unbound<I, SC, C>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    id: UnboundId,
    remote: SocketAddr<I::Addr>,
    netstack_buffers: C::NetstackEndBuffers,
) -> Result<ConnectionId, ConnectError>
where
    I: IpExt,
    C: TcpNonSyncContext,
    SC: BufferTransportIpContext<I, C, Buf<Vec<u8>>> + TcpSyncContext<I, C>,
{
    let ip_sock = sync_ctx
        .new_ip_socket(ctx, None, None, remote.ip, IpProto::Tcp.into(), None)
        .map_err(|err| match err {
            IpSockCreationError::Route(_) => ConnectError::NoRoute,
        })?;
    let local_port =
        sync_ctx.with_tcp_sockets_mut(|TcpSockets { port_alloc, inactive: _, socketmap }| {
            match port_alloc.try_alloc(&*ip_sock.local_ip(), &socketmap) {
                Some(port) => Ok(NonZeroU16::new(port).expect("ephemeral ports must be non-zero")),
                None => Err(ConnectError::NoPort),
            }
        })?;
    let conn_id = connect_inner(sync_ctx, ctx, ip_sock, local_port, remote.port, netstack_buffers)?;
    let _: Option<_> = sync_ctx.with_tcp_sockets_mut(|sockets| sockets.inactive.remove(id.into()));
    Ok(conn_id)
}

fn connect_inner<I, SC, C>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    ip_sock: IpSock<I, SC::DeviceId>,
    local_port: NonZeroU16,
    remote_port: NonZeroU16,
    netstack_buffers: C::NetstackEndBuffers,
) -> Result<ConnectionId, ConnectError>
where
    I: IpExt,
    C: TcpNonSyncContext,
    SC: TcpSyncContext<I, C> + BufferTransportIpContext<I, C, Buf<Vec<u8>>>,
{
    let (syn, conn_addr, conn_id) = sync_ctx.with_isn_generator_and_tcp_sockets_mut(
        |isn, TcpSockets { port_alloc: _, inactive: _, socketmap }| {
            let isn = isn.generate(
                ctx.now(),
                SocketAddr { ip: ip_sock.local_ip().clone(), port: local_port },
                SocketAddr { ip: ip_sock.remote_ip().clone(), port: remote_port },
            );
            let conn_addr = ConnAddr {
                ip: ConnIpAddr {
                    local: (ip_sock.local_ip().clone(), local_port),
                    remote: (ip_sock.remote_ip().clone(), remote_port),
                },
                // TODO(https://fxbug.dev/102103): Support SO_BINDTODEVICE.
                device: None,
            };
            let now = ctx.now();
            let (syn_sent, syn) = Closed::<Initial>::connect(isn, now, netstack_buffers);
            let conn_id = socketmap
                .conns_mut()
                .try_insert(
                    conn_addr.clone(),
                    Connection {
                        acceptor: None,
                        state: State::SynSent(syn_sent),
                        ip_sock: ip_sock.clone(),
                    },
                    // TODO(https://fxbug.dev/101596): Support sharing for TCP sockets.
                    (),
                )
                .expect("failed to insert connection");

            (syn, conn_addr, conn_id)
        },
    );

    sync_ctx
        .send_ip_packet(ctx, &ip_sock, tcp_serialize_segment(syn, conn_addr.ip), None)
        .map_err(|(body, err)| {
            warn!("tcp: failed to send ip packet {:?}: {:?}", body, err);
            assert_matches!(
                sync_ctx
                    .with_tcp_sockets_mut(|sockets| sockets.socketmap.conns_mut().remove(&conn_id)),
                Some(_)
            );
            ConnectError::NoRoute
        })?;
    Ok(conn_id)
}

/// Call this function whenever a socket can push out more data. That means either:
///
/// - A retransmission timer fires.
/// - An ack received from peer so that our send window is enlarged.
/// - The user puts data into the buffer and we are notified.
fn do_send<I, SC, C>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    conn: ConnectionId,
) -> Result<(), crate::ip::socket::IpSockSendError>
where
    I: IpExt,
    C: TcpNonSyncContext,
    SC: TcpSyncContext<I, C> + BufferTransportIpContext<I, C, Buf<Vec<u8>>>,
{
    let send_more = sync_ctx.with_tcp_sockets_mut(|sockets| {
        let (conn, (), addr) =
            sockets.socketmap.conns_mut().get_by_id_mut(&conn).expect("invalid connection id");
        conn.state
            .poll_send(DEFAULT_MAXIMUM_SEGMENT_SIZE, ctx.now())
            .map(|seg| (conn.ip_sock.clone(), tcp_serialize_segment(seg, addr.ip.clone())))
    });
    if let Some((ip_sock, ser)) = send_more {
        sync_ctx.send_ip_packet(ctx, &ip_sock, ser, None).map_err(|(body, err)| err)?;
    }
    Ok(())
}

impl From<ListenerId> for MaybeListenerId {
    fn from(ListenerId(x): ListenerId) -> Self {
        Self(x)
    }
}

impl From<BoundId> for MaybeListenerId {
    fn from(BoundId(x): BoundId) -> Self {
        Self(x)
    }
}

impl From<usize> for MaybeListenerId {
    fn from(x: usize) -> Self {
        Self(x)
    }
}

impl Into<usize> for MaybeListenerId {
    fn into(self) -> usize {
        let Self(x) = self;
        x
    }
}

impl From<usize> for ConnectionId {
    fn from(x: usize) -> Self {
        Self(x)
    }
}

impl Into<usize> for ListenerId {
    fn into(self) -> usize {
        let Self(x) = self;
        x
    }
}

impl Into<usize> for ConnectionId {
    fn into(self) -> usize {
        let Self(x) = self;
        x
    }
}

impl Into<usize> for UnboundId {
    fn into(self) -> usize {
        let Self(x) = self;
        x
    }
}

#[cfg(test)]
mod tests {
    use const_unwrap::const_unwrap_option;
    use core::{cell::RefCell, fmt::Debug};
    use fakealloc::rc::Rc;
    use net_types::ip::{AddrSubnet, Ip, Ipv4, Ipv6, Ipv6SourceAddr};
    use packet::{ContiguousBuffer, ParseBuffer as _, Serializer};
    use packet_formats::tcp::{TcpParseArgs, TcpSegment, TcpSegmentBuilder};
    use specialize_ip_macro::ip_test;
    use test_case::test_case;

    use crate::{
        context::testutil::{
            DummyCtx, DummyInstant, DummyNetwork, DummyNetworkLinks, DummyNonSyncCtx, DummySyncCtx,
            InstantAndData, PendingFrameData, StepResult,
        },
        ip::{
            device::state::{
                AddrConfig, AddressState, IpDeviceState, IpDeviceStateIpExt, Ipv6AddressEntry,
            },
            socket::{testutil::DummyIpSocketCtx, BufferIpSocketHandler, IpSocketHandler},
            BufferIpTransportContext as _, DummyDeviceId, SendIpPacketMeta,
        },
        socket::SocketTypeStateMut,
        testutil::{set_logger_for_test, FakeCryptoRng, TestIpExt},
        transport::tcp::{
            buffer::{Buffer, RingBuffer, SendPayload},
            segment::{self, Payload, Segment},
            UserError,
        },
    };

    use super::*;

    trait TcpTestIpExt: IpExt + TestIpExt + IpDeviceStateIpExt<DummyInstant> {
        fn recv_src_addr(addr: Self::Addr) -> Self::RecvSrcAddr;

        fn new_device_state(addr: Self::Addr, prefix: u8) -> IpDeviceState<DummyInstant, Self>;
    }

    struct TcpState<I: TcpTestIpExt> {
        isn_generator: IsnGenerator<DummyInstant>,
        sockets: TcpSockets<I, DummyDeviceId, TcpNonSyncCtx>,
        ip_socket_ctx: DummyIpSocketCtx<I, DummyDeviceId>,
    }

    type TcpCtx<I> = DummyCtx<
        TcpState<I>,
        (),
        SendIpPacketMeta<I, DummyDeviceId, SpecifiedAddr<<I as Ip>::Addr>>,
        (),
        DummyDeviceId,
        (),
    >;

    type TcpSyncCtx<I> = DummySyncCtx<
        TcpState<I>,
        SendIpPacketMeta<I, DummyDeviceId, SpecifiedAddr<<I as Ip>::Addr>>,
        DummyDeviceId,
    >;

    type TcpNonSyncCtx = DummyNonSyncCtx<(), (), ()>;

    impl Buffer for Rc<RefCell<RingBuffer>> {
        fn len(&self) -> usize {
            self.borrow().len()
        }

        fn cap(&self) -> usize {
            self.borrow().cap()
        }

        fn empty() -> Self {
            Rc::new(RefCell::new(RingBuffer::empty()))
        }
    }

    impl ReceiveBuffer for Rc<RefCell<RingBuffer>> {
        type Residual = Self;

        fn write_at<P: Payload>(&mut self, offset: usize, data: &P) -> usize {
            self.borrow_mut().write_at(offset, data)
        }

        fn make_readable(&mut self, count: usize) {
            self.borrow_mut().make_readable(count)
        }
    }

    #[derive(Debug, Default)]
    pub struct TestSendBuffer(Rc<RefCell<Vec<u8>>>, RingBuffer);

    impl Buffer for TestSendBuffer {
        fn len(&self) -> usize {
            self.1.len() + self.0.borrow().len()
        }

        fn cap(&self) -> usize {
            self.1.cap() + self.0.borrow().capacity()
        }

        fn empty() -> Self {
            TestSendBuffer(Rc::new(RefCell::new(Vec::new())), RingBuffer::empty())
        }
    }

    impl SendBuffer for TestSendBuffer {
        fn mark_read(&mut self, count: usize) {
            self.1.mark_read(count)
        }

        fn peek_with<'a, F, R>(&'a mut self, offset: usize, f: F) -> R
        where
            F: FnOnce(SendPayload<'a>) -> R,
        {
            let v = &self.0;
            let rb = &mut self.1;
            if !v.borrow().is_empty() {
                let len = (rb.cap() - rb.len()).min(v.borrow().len());
                let rest = v.borrow_mut().split_off(len);
                let first = v.replace(rest);
                assert_eq!(rb.enqueue_data(&first[..]), len);
            }
            rb.peek_with(offset, f)
        }
    }

    impl TcpNonSyncContext for TcpNonSyncCtx {
        type ReceiveBuffer = Rc<RefCell<RingBuffer>>;
        type SendBuffer = TestSendBuffer;
        type ClientEndBuffers = (Rc<RefCell<RingBuffer>>, Rc<RefCell<Vec<u8>>>);
        type NetstackEndBuffers = (Rc<RefCell<RingBuffer>>, Rc<RefCell<Vec<u8>>>);

        fn on_new_connection(&mut self, listener: ListenerId) {}
        fn new_passive_open_buffers(
        ) -> (Self::ReceiveBuffer, Self::SendBuffer, Self::ClientEndBuffers) {
            let rb = Rc::new(RefCell::new(RingBuffer::default()));
            let sb = Rc::new(RefCell::new(Vec::new()));
            (Rc::clone(&rb), TestSendBuffer(Rc::clone(&sb), RingBuffer::default()), (rb, sb))
        }
    }

    impl IntoBuffers<Rc<RefCell<RingBuffer>>, TestSendBuffer>
        for (Rc<RefCell<RingBuffer>>, Rc<RefCell<Vec<u8>>>)
    {
        fn into_buffers(self) -> (Rc<RefCell<RingBuffer>>, TestSendBuffer) {
            let (rb, sb) = self;
            (rb, TestSendBuffer(sb, Default::default()))
        }
    }

    impl<I: TcpTestIpExt> TransportIpContext<I, TcpNonSyncCtx> for TcpSyncCtx<I>
    where
        TcpSyncCtx<I>: IpSocketHandler<I, TcpNonSyncCtx>,
        TcpSyncCtx<I>: IpDeviceIdContext<I, DeviceId = DummyDeviceId>,
    {
        fn get_device_with_assigned_addr(
            &self,
            _addr: SpecifiedAddr<I::Addr>,
        ) -> Option<DummyDeviceId> {
            Some(DummyDeviceId)
        }
    }

    impl<I: TcpTestIpExt> TcpSyncContext<I, TcpNonSyncCtx> for TcpSyncCtx<I> {
        fn with_isn_generator_and_tcp_sockets_mut<
            O,
            F: FnOnce(
                &IsnGenerator<DummyInstant>,
                &mut TcpSockets<I, DummyDeviceId, TcpNonSyncCtx>,
            ) -> O,
        >(
            &mut self,
            cb: F,
        ) -> O {
            let TcpState { isn_generator, sockets, ip_socket_ctx: _ } = self.get_mut();
            cb(isn_generator, sockets)
        }
    }

    impl<I: TcpTestIpExt> AsMut<DummyIpSocketCtx<I, DummyDeviceId>> for TcpState<I> {
        fn as_mut(&mut self) -> &mut DummyIpSocketCtx<I, DummyDeviceId> {
            &mut self.ip_socket_ctx
        }
    }

    impl<I: TcpTestIpExt> AsRef<DummyIpSocketCtx<I, DummyDeviceId>> for TcpState<I> {
        fn as_ref(&self) -> &DummyIpSocketCtx<I, DummyDeviceId> {
            &self.ip_socket_ctx
        }
    }

    impl<I: TcpTestIpExt> TcpState<I> {
        fn new(addr: SpecifiedAddr<I::Addr>, peer: SpecifiedAddr<I::Addr>, prefix: u8) -> Self {
            let ip_socket_ctx = DummyIpSocketCtx::<I, _>::with_devices_state(core::iter::once((
                DummyDeviceId,
                I::new_device_state(*addr, prefix),
                alloc::vec![peer],
            )));
            Self {
                isn_generator: Default::default(),
                sockets: TcpSockets {
                    inactive: IdMap::new(),
                    socketmap: BoundSocketMap::default(),
                    port_alloc: PortAlloc::new(&mut FakeCryptoRng::new_xorshift(0)),
                },
                ip_socket_ctx,
            }
        }
    }

    const LOCAL: &'static str = "local";
    const REMOTE: &'static str = "remote";
    const PORT_1: NonZeroU16 = const_unwrap_option(NonZeroU16::new(42));
    const PORT_2: NonZeroU16 = const_unwrap_option(NonZeroU16::new(43));

    impl TcpTestIpExt for Ipv4 {
        fn recv_src_addr(addr: Self::Addr) -> Self::RecvSrcAddr {
            addr
        }

        fn new_device_state(addr: Self::Addr, prefix: u8) -> IpDeviceState<DummyInstant, Self> {
            let mut device_state = IpDeviceState::default();
            device_state
                .add_addr(AddrSubnet::new(addr, prefix).unwrap())
                .expect("failed to add address");
            device_state
        }
    }

    impl TcpTestIpExt for Ipv6 {
        fn recv_src_addr(addr: Self::Addr) -> Self::RecvSrcAddr {
            Ipv6SourceAddr::new(addr).unwrap()
        }

        fn new_device_state(addr: Self::Addr, prefix: u8) -> IpDeviceState<DummyInstant, Self> {
            let mut device_state = IpDeviceState::default();
            device_state
                .add_addr(Ipv6AddressEntry::new(
                    AddrSubnet::new(addr, prefix).unwrap(),
                    AddressState::Assigned,
                    AddrConfig::Manual,
                ))
                .expect("failed to add address");
            device_state
        }
    }

    fn new_test_net<I: TcpTestIpExt>() -> DummyNetwork<
        &'static str,
        SendIpPacketMeta<I, DummyDeviceId, SpecifiedAddr<I::Addr>>,
        TcpCtx<I>,
        impl DummyNetworkLinks<
            SendIpPacketMeta<I, DummyDeviceId, SpecifiedAddr<I::Addr>>,
            SendIpPacketMeta<I, DummyDeviceId, SpecifiedAddr<I::Addr>>,
            &'static str,
        >,
    > {
        DummyNetwork::new(
            [
                (
                    LOCAL,
                    TcpCtx::with_state(TcpState::new(
                        I::DUMMY_CONFIG.local_ip,
                        I::DUMMY_CONFIG.remote_ip,
                        I::DUMMY_CONFIG.subnet.prefix(),
                    )),
                ),
                (
                    REMOTE,
                    TcpCtx::with_state(TcpState::new(
                        I::DUMMY_CONFIG.remote_ip,
                        I::DUMMY_CONFIG.local_ip,
                        I::DUMMY_CONFIG.subnet.prefix(),
                    )),
                ),
            ],
            move |net, meta: SendIpPacketMeta<I, _, _>| {
                if net == LOCAL {
                    alloc::vec![(REMOTE, meta, None)]
                } else {
                    alloc::vec![(LOCAL, meta, None)]
                }
            },
        )
    }

    fn handle_frame<I: TcpTestIpExt>(
        TcpCtx { sync_ctx, non_sync_ctx }: &mut TcpCtx<I>,
        meta: SendIpPacketMeta<I, DummyDeviceId, SpecifiedAddr<I::Addr>>,
        buffer: Buf<Vec<u8>>,
    ) where
        TcpSyncCtx<I>: BufferIpSocketHandler<I, TcpNonSyncCtx, Buf<Vec<u8>>>
            + IpDeviceIdContext<I, DeviceId = DummyDeviceId>,
    {
        TcpIpTransportContext::receive_ip_packet(
            sync_ctx,
            non_sync_ctx,
            DummyDeviceId,
            I::recv_src_addr(*meta.src_ip),
            meta.dst_ip,
            buffer,
        )
        .expect("failed to deliver bytes");
    }

    fn panic_if_any_timer<Ctx, NonSyncCtx, Timer: Debug>(
        _sc: &mut Ctx,
        _c: &mut NonSyncCtx,
        timer: Timer,
    ) {
        panic!("unexpected timer fired: {:?}", timer)
    }

    /// The following test sets up two connected testing context - one as the
    /// server and the other as the client. Tests if a connection can be
    /// established using `bind`, `listen`, `connect` and `accept`.
    ///
    /// # Arguments
    ///
    /// * `listen_addr` - The address to listen on.
    /// * `bind_client` - Whether to bind the client before connecting.
    fn bind_listen_connect_accept_inner<I: Ip + TcpTestIpExt>(
        listen_addr: I::Addr,
        bind_client: bool,
    ) where
        TcpSyncCtx<I>: BufferIpSocketHandler<I, TcpNonSyncCtx, Buf<Vec<u8>>>
            + IpDeviceIdContext<I, DeviceId = DummyDeviceId>,
    {
        let mut net = new_test_net::<I>();

        let backlog = NonZeroUsize::new(1).unwrap();
        let server = net.with_context(REMOTE, |TcpCtx { sync_ctx, non_sync_ctx }| {
            let conn = create_socket(sync_ctx, non_sync_ctx);
            let conn = bind(sync_ctx, non_sync_ctx, conn, listen_addr, Some(PORT_1))
                .expect("failed to bind the server socket");
            listen(sync_ctx, non_sync_ctx, conn, backlog)
        });

        let client_rcv_end = Rc::new(RefCell::new(RingBuffer::default()));
        let client_snd_end = Rc::new(RefCell::new(Vec::new()));
        let client = net.with_context(LOCAL, |TcpCtx { sync_ctx, non_sync_ctx }| {
            let conn = create_socket(sync_ctx, non_sync_ctx);
            if bind_client {
                let conn =
                    bind(sync_ctx, non_sync_ctx, conn, *I::DUMMY_CONFIG.local_ip, Some(PORT_1))
                        .expect("failed to bind the client socket");
                connect_bound(
                    sync_ctx,
                    non_sync_ctx,
                    conn,
                    SocketAddr { ip: I::DUMMY_CONFIG.remote_ip, port: PORT_1 },
                    (Rc::clone(&client_rcv_end), Rc::clone(&client_snd_end)),
                )
                .expect("failed to connect")
            } else {
                connect_unbound(
                    sync_ctx,
                    non_sync_ctx,
                    conn,
                    SocketAddr { ip: I::DUMMY_CONFIG.remote_ip, port: PORT_1 },
                    (Rc::clone(&client_rcv_end), Rc::clone(&client_snd_end)),
                )
                .expect("failed to connect")
            }
        });
        // Step once for the SYN packet to be sent.
        let _: StepResult = net.step(handle_frame, panic_if_any_timer);
        // The listener should create a pending socket.
        assert_matches!(
            net.sync_ctx(REMOTE).get_mut().sockets.get_listener_by_id_mut(server),
            Some(Listener { backlog: _, ready, pending }) => {
                assert_eq!(ready.len(), 0);
                assert_eq!(pending.len(), 1);
            }
        );
        // The handshake is not done, calling accept here should not succeed.
        net.with_context(REMOTE, |TcpCtx { sync_ctx, non_sync_ctx }| {
            assert_matches!(accept(sync_ctx, non_sync_ctx, server), Err(AcceptError::WouldBlock));
        });

        // Step the test network until the handshake is done.
        net.run_until_idle(handle_frame, panic_if_any_timer);
        let (accepted, addr, (accepted_rcv_end, accepted_snd_end)) =
            net.with_context(REMOTE, |TcpCtx { sync_ctx, non_sync_ctx }| {
                accept(sync_ctx, non_sync_ctx, server).expect("failed to accept")
            });
        if bind_client {
            assert_eq!(addr, SocketAddr { ip: I::DUMMY_CONFIG.local_ip, port: PORT_1 });
        } else {
            assert_eq!(addr.ip, I::DUMMY_CONFIG.local_ip);
        }

        let mut assert_connected = |name: &'static str, conn_id: ConnectionId| {
            let (state, (), _): (_, _, &ConnAddr<_, _, _, _>) = net
                .sync_ctx(name)
                .get_mut()
                .sockets
                .socketmap
                .conns_mut()
                .get_by_id_mut(&conn_id)
                .expect("failed to retrieve the client socket");
            assert_matches!(
                state,
                Connection { acceptor: None, state: State::Established(_), ip_sock: _ }
            )
        };

        assert_connected(LOCAL, client);
        assert_connected(REMOTE, accepted);

        for snd_end in [client_snd_end, accepted_snd_end] {
            snd_end.borrow_mut().extend_from_slice(b"Hello");
        }

        for (c, id) in [(LOCAL, client), (REMOTE, accepted)] {
            net.with_context(c, |TcpCtx { sync_ctx, non_sync_ctx }| {
                assert_matches!(do_send(sync_ctx, non_sync_ctx, id), Ok(()));
            })
        }
        net.run_until_idle(handle_frame, panic_if_any_timer);

        for rcv_end in [client_rcv_end, accepted_rcv_end] {
            assert_eq!(
                rcv_end.borrow_mut().read_with(|avail| {
                    let avail = avail.concat();
                    assert_eq!(avail, b"Hello");
                    avail.len()
                }),
                5
            );
        }

        // Check the listener is in correct state.
        assert_eq!(
            net.sync_ctx(REMOTE).get_mut().sockets.get_listener_by_id_mut(server),
            Some(&mut Listener::new(backlog)),
        );
    }

    #[ip_test]
    fn bind_listen_connect_accept<I: Ip + TcpTestIpExt>()
    where
        TcpSyncCtx<I>: BufferIpSocketHandler<I, TcpNonSyncCtx, Buf<Vec<u8>>>
            + IpDeviceIdContext<I, DeviceId = DummyDeviceId>,
    {
        set_logger_for_test();
        for bind_client in [true, false] {
            for listen_addr in [I::UNSPECIFIED_ADDRESS, *I::DUMMY_CONFIG.remote_ip] {
                bind_listen_connect_accept_inner(listen_addr, bind_client);
            }
        }
    }

    #[ip_test]
    #[test_case(*<I as TestIpExt>::DUMMY_CONFIG.local_ip; "same addr")]
    #[test_case(I::UNSPECIFIED_ADDRESS; "any addr")]
    fn bind_conflict<I: Ip + TcpTestIpExt>(conflict_addr: I::Addr)
    where
        TcpSyncCtx<I>: BufferIpSocketHandler<I, TcpNonSyncCtx, Buf<Vec<u8>>>
            + IpDeviceIdContext<I, DeviceId = DummyDeviceId>,
    {
        set_logger_for_test();
        let TcpCtx { mut sync_ctx, mut non_sync_ctx } = TcpCtx::with_state(TcpState::new(
            I::DUMMY_CONFIG.local_ip,
            I::DUMMY_CONFIG.local_ip,
            I::DUMMY_CONFIG.subnet.prefix(),
        ));
        let s1 = create_socket(&mut sync_ctx, &mut non_sync_ctx);
        let s2 = create_socket(&mut sync_ctx, &mut non_sync_ctx);

        let _b1 =
            bind(&mut sync_ctx, &mut non_sync_ctx, s1, *I::DUMMY_CONFIG.local_ip, Some(PORT_1))
                .expect("first bind should succeed");
        assert_matches!(
            bind(&mut sync_ctx, &mut non_sync_ctx, s2, conflict_addr, Some(PORT_1)),
            Err(BindError::Conflict)
        );
        let _b2 = bind(&mut sync_ctx, &mut non_sync_ctx, s2, conflict_addr, Some(PORT_2))
            .expect("able to rebind to a free address");
    }

    // The test verifies that if client tries to connect to a closed port on
    // server, the connection is aborted and RST is received.
    #[ip_test]
    fn connect_reset<I: Ip + TcpTestIpExt>()
    where
        TcpSyncCtx<I>: BufferIpSocketHandler<I, TcpNonSyncCtx, Buf<Vec<u8>>>
            + IpDeviceIdContext<I, DeviceId = DummyDeviceId>,
    {
        set_logger_for_test();
        let mut net = new_test_net::<I>();

        let client = net.with_context(LOCAL, |TcpCtx { sync_ctx, non_sync_ctx }| {
            let conn = create_socket(sync_ctx, non_sync_ctx);
            let conn = bind(sync_ctx, non_sync_ctx, conn, *I::DUMMY_CONFIG.local_ip, Some(PORT_1))
                .expect("failed to bind the client socket");
            connect_bound(
                sync_ctx,
                non_sync_ctx,
                conn,
                SocketAddr { ip: I::DUMMY_CONFIG.remote_ip, port: PORT_1 },
                (Rc::new(RefCell::new(RingBuffer::default())), Rc::new(RefCell::new(Vec::new()))),
            )
            .expect("failed to connect")
        });

        // Step one time for SYN packet to be delivered.
        let _: StepResult = net.step(handle_frame, panic_if_any_timer);
        // Assert that we got a RST back.
        net.collect_frames();
        assert_matches!(
            &net.iter_pending_frames().collect::<Vec<_>>()[..],
            [InstantAndData(_instant, PendingFrameData {
                dst_context: _,
                meta,
                frame,
            })] => {
            let mut buffer = Buf::new(frame, ..);
            let parsed = buffer.parse_with::<_, TcpSegment<_>>(
                TcpParseArgs::new(*meta.src_ip, *meta.dst_ip)
            ).expect("failed to parse");
            assert!(parsed.rst())
        });

        net.run_until_idle(handle_frame, panic_if_any_timer);
        // Finally, the connection should be reset.
        let (state, (), _): &(_, _, ConnAddr<_, _, _, _>) = net
            .sync_ctx(LOCAL)
            .get_ref()
            .sockets
            .socketmap
            .conns()
            .get_by_id(&client)
            .expect("failed to retrieve the client socket");
        assert_matches!(
            state,
            Connection {
                acceptor: None,
                state: State::Closed(Closed { reason: UserError::ConnectionReset }),
                ip_sock: _
            }
        );
    }
}
