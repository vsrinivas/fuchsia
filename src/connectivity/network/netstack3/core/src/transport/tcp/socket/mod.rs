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
    ops::RangeInclusive,
};
use nonzero_ext::nonzero;

use assert_matches::assert_matches;
use derivative::Derivative;
use log::warn;
use net_types::{
    ip::{
        GenericOverIp, Ip, IpAddr, IpAddress, IpInvariant as IpInv, IpVersion, Ipv4, Ipv4Addr,
        Ipv6, Ipv6Addr,
    },
    SpecifiedAddr,
};
use packet::Buf;
use packet_formats::ip::IpProto;
use rand::RngCore;

use crate::{
    algorithm::{PortAlloc, PortAllocImpl},
    context::TimerContext,
    data_structures::{
        id_map::{Entry as IdMapEntry, IdMap},
        socketmap::{IterShadows as _, SocketMap, Tagged},
    },
    ip::{
        socket::{
            BufferIpSocketHandler as _, DefaultSendOptions, IpSock, IpSockCreationError,
            IpSocketHandler as _,
        },
        BufferTransportIpContext, IpDeviceId, IpDeviceIdContext, IpExt, TransportIpContext as _,
    },
    socket::{
        address::{ConnAddr, ConnIpAddr, IpPortSpec, ListenerAddr, ListenerIpAddr},
        AddrVec, Bound, BoundSocketMap, IncompatibleError, InsertError, RemoveResult,
        SocketAddrTypeTag, SocketMapAddrStateSpec, SocketMapConflictPolicy, SocketMapStateSpec,
        SocketTypeState as _, SocketTypeStateEntry as _, SocketTypeStateMut as _,
    },
    transport::tcp::{
        buffer::{IntoBuffers, ReceiveBuffer, SendBuffer},
        socket::{demux::tcp_serialize_segment, isn::IsnGenerator},
        state::{CloseError, Closed, Initial, State, Takeable},
        BufferSizes,
    },
    DeviceId, Instant, NonSyncContext, SyncCtx,
};

/// Per RFC 879 section 1 (https://tools.ietf.org/html/rfc879#section-1):
///
/// THE TCP MAXIMUM SEGMENT SIZE IS THE IP MAXIMUM DATAGRAM SIZE MINUS
/// FORTY.
///   The default IP Maximum Datagram Size is 576.
///   The default TCP Maximum Segment Size is 536.
const DEFAULT_MAXIMUM_SEGMENT_SIZE: u32 = 536;

/// Timer ID for TCP connections.
#[derive(Debug, PartialEq, Eq, Clone, Copy, Hash)]
pub struct TimerId(MaybeClosedConnectionId, IpVersion);

/// Non-sync context for TCP.
///
/// The relationship between buffers defined in the context is as follows:
///
/// The Bindings will receive the `ReturnedBuffers` so that it can: 1. give the
/// application a handle to read/write data; 2. Observe whatever signal required
/// from the application so that it can inform Core. The peer end of returned
/// handle will be held by the state machine inside the netstack. Specialized
/// receive/send buffers will be derived from `ProvidedBuffers` from Bindings.
///
/// +-------------------------------+
/// |       +--------------+        |
/// |       |   returned   |        |
/// |       |    buffers   |        |
/// |       +------+-------+        |
/// |              |     application|
/// +--------------+----------------+
///                |
/// +--------------+----------------+
/// |              |        netstack|
/// |   +---+------+-------+---+    |
/// |   |   |  provided    |   |    |
/// |   | +-+-  buffers   -+-+ |    |
/// |   +-+-+--------------+-+-+    |
/// |     v                  v      |
/// |receive buffer     send buffer |
/// +-------------------------------+
pub trait TcpNonSyncContext: TimerContext<TimerId> {
    /// Receive buffer used by TCP.
    type ReceiveBuffer: ReceiveBuffer;
    /// Send buffer used by TCP.
    type SendBuffer: SendBuffer;
    /// The object that will be returned by the state machine when a passive
    /// open connection becomes established. The bindings can use this object
    /// to read/write bytes from/into the created buffers.
    type ReturnedBuffers: Debug;
    /// The object that is needed from the bindings to initiate a connection,
    /// it is provided by the bindings and will be later used to construct
    /// buffers when the connection becomes established.
    type ProvidedBuffers: Debug + Takeable + IntoBuffers<Self::ReceiveBuffer, Self::SendBuffer>;

    /// A new connection is ready to be accepted on the listener.
    fn on_new_connection(&mut self, listener: ListenerId);
    /// Creates new buffers and returns the object that Bindings need to
    /// read/write from/into the created buffers.
    fn new_passive_open_buffers(
        buffer_sizes: BufferSizes,
    ) -> (Self::ReceiveBuffer, Self::SendBuffer, Self::ReturnedBuffers);
}

/// Sync context for TCP.
pub(crate) trait TcpSyncContext<I: IpExt, C: TcpNonSyncContext>:
    IpDeviceIdContext<I>
{
    type IpTransportCtx: BufferTransportIpContext<I, C, Buf<Vec<u8>>, DeviceId = Self::DeviceId>;

    /// Calls the function with a `Self::IpTransportCtx`, immutable reference to
    /// an initial sequence number generator and a mutable reference to TCP
    /// socket state.
    fn with_ip_transport_ctx_isn_generator_and_tcp_sockets_mut<
        O,
        F: FnOnce(
            &mut Self::IpTransportCtx,
            &IsnGenerator<C::Instant>,
            &mut TcpSockets<I, Self::DeviceId, C>,
        ) -> O,
    >(
        &mut self,
        cb: F,
    ) -> O;

    /// Calls the function with a `Self::IpTransportCtx` and a mutable reference
    /// to TCP socket state.
    fn with_ip_transport_ctx_and_tcp_sockets_mut<
        O,
        F: FnOnce(&mut Self::IpTransportCtx, &mut TcpSockets<I, Self::DeviceId, C>) -> O,
    >(
        &mut self,
        cb: F,
    ) -> O {
        self.with_ip_transport_ctx_isn_generator_and_tcp_sockets_mut(|ctx, _isn, sockets| {
            cb(ctx, sockets)
        })
    }

    /// Calls the function with a mutable reference to TCP socket state.
    fn with_tcp_sockets_mut<O, F: FnOnce(&mut TcpSockets<I, Self::DeviceId, C>) -> O>(
        &mut self,
        cb: F,
    ) -> O {
        self.with_ip_transport_ctx_isn_generator_and_tcp_sockets_mut(|_ctx, _isn, sockets| {
            cb(sockets)
        })
    }

    /// Calls the function with an immutable reference to TCP socket state.
    fn with_tcp_sockets<O, F: FnOnce(&TcpSockets<I, Self::DeviceId, C>) -> O>(&self, cb: F) -> O;
}

/// Socket address includes the ip address and the port number.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, GenericOverIp)]
pub struct SocketAddr<A: IpAddress> {
    /// The IP component of the address.
    pub ip: SpecifiedAddr<A>,
    /// The port component of the address.
    pub port: NonZeroU16,
}

impl<A: IpAddress> From<SocketAddr<A>> for IpAddr<SocketAddr<Ipv4Addr>, SocketAddr<Ipv6Addr>> {
    fn from(
        SocketAddr { ip, port }: SocketAddr<A>,
    ) -> IpAddr<SocketAddr<Ipv4Addr>, SocketAddr<Ipv6Addr>> {
        match ip.into() {
            IpAddr::V4(ip) => IpAddr::V4(SocketAddr { ip, port }),
            IpAddr::V6(ip) => IpAddr::V6(SocketAddr { ip, port }),
        }
    }
}

/// An implementation of [`IpTransportContext`] for TCP.
pub(crate) enum TcpIpTransportContext {}

/// Uninstantiatable type for implementing [`SocketMapStateSpec`].
struct TcpSocketSpec<Ip, Device, NonSyncContext>(PhantomData<(Ip, Device, NonSyncContext)>, Never);

impl<I: IpExt, D: IpDeviceId, C: TcpNonSyncContext> SocketMapStateSpec for TcpSocketSpec<I, D, C> {
    type ListenerId = MaybeListenerId;
    type ConnId = MaybeClosedConnectionId;

    type ListenerState = MaybeListener<C::ReturnedBuffers>;
    type ConnState =
        Connection<I, D, C::Instant, C::ReceiveBuffer, C::SendBuffer, C::ProvidedBuffers>;

    type ListenerSharingState = ();
    type ConnSharingState = ();
    type AddrVecTag = SocketAddrTypeTag<()>;

    type ListenerAddrState = MaybeListenerId;
    type ConnAddrState = MaybeClosedConnectionId;
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
        let addr = AddrVec::Listen(addr.clone());
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

impl<A: IpAddress, D, LI, RI> Tagged<ConnAddr<A, D, LI, RI>> for MaybeClosedConnectionId {
    type Tag = SocketAddrTypeTag<()>;
    fn tag(&self, address: &ConnAddr<A, D, LI, RI>) -> Self::Tag {
        (address, ()).into()
    }
}

#[derive(Debug, Derivative, Clone)]
#[derivative(Default(bound = ""))]
#[cfg_attr(test, derive(PartialEq))]
struct Unbound<D> {
    bound_device: Option<D>,
    buffer_sizes: BufferSizes,
}

/// Holds all the TCP socket states.
pub struct TcpSockets<I: IpExt, D: IpDeviceId, C: TcpNonSyncContext> {
    port_alloc: PortAlloc<BoundSocketMap<IpPortSpec<I, D>, TcpSocketSpec<I, D, C>>>,
    inactive: IdMap<Unbound<D>>,
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
    ) -> Option<&mut Listener<C::ReturnedBuffers>> {
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
    ip_sock: IpSock<I, D, DefaultSendOptions>,
    /// The user has indicated that this connection will never be used again, we
    /// keep the connection in the socketmap to perform the shutdown but it will
    /// be auto removed once the state reaches Closed.
    defunct: bool,
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
    buffer_sizes: BufferSizes,
    // If ip sockets can be half-specified so that only the local address
    // is needed, we can construct an ip socket here to be reused.
}

impl<PassiveOpen> Listener<PassiveOpen> {
    fn new(backlog: NonZeroUsize, buffer_sizes: BufferSizes) -> Self {
        Self { backlog, ready: VecDeque::new(), pending: Vec::new(), buffer_sizes }
    }
}

#[derive(Clone, Debug)]
#[cfg_attr(test, derive(Eq, PartialEq))]
struct BoundState {
    buffer_sizes: BufferSizes,
}

/// Represents either a bound socket or a listener socket.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq))]
enum MaybeListener<PassiveOpen> {
    Bound(BoundState),
    Listener(Listener<PassiveOpen>),
}

impl<PassiveOpen> MaybeListener<PassiveOpen> {
    fn buffer_sizes(&self) -> BufferSizes {
        match self {
            Self::Bound(BoundState { buffer_sizes }) => buffer_sizes,
            Self::Listener(Listener { backlog: _, ready: _, pending: _, buffer_sizes }) => {
                buffer_sizes
            }
        }
        .clone()
    }
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
/// The ID to a connection socket that might have been defunct.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct MaybeClosedConnectionId(usize);
/// The ID to a connection socket that has never been closed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ConnectionId(usize);

impl ConnectionId {
    fn get_from_socketmap<I: IpExt, D: IpDeviceId, C: TcpNonSyncContext>(
        self,
        socketmap: &BoundSocketMap<IpPortSpec<I, D>, TcpSocketSpec<I, D, C>>,
    ) -> (
        &Connection<I, D, C::Instant, C::ReceiveBuffer, C::SendBuffer, C::ProvidedBuffers>,
        (),
        &ConnAddr<I::Addr, D, NonZeroU16, NonZeroU16>,
    ) {
        let (conn, (), addr) =
            socketmap.conns().get_by_id(&self.into()).expect("invalid ConnectionId: not found");
        assert!(!conn.defunct, "invalid ConnectionId: already defunct");
        (conn, (), addr)
    }

    fn get_from_socketmap_mut<I: IpExt, D: IpDeviceId, C: TcpNonSyncContext>(
        self,
        socketmap: &mut BoundSocketMap<IpPortSpec<I, D>, TcpSocketSpec<I, D, C>>,
    ) -> (
        &mut Connection<I, D, C::Instant, C::ReceiveBuffer, C::SendBuffer, C::ProvidedBuffers>,
        (),
        &ConnAddr<I::Addr, D, NonZeroU16, NonZeroU16>,
    ) {
        let (conn, (), addr) = socketmap
            .conns_mut()
            .get_by_id_mut(&self.into())
            .expect("invalid ConnectionId: not found");
        assert!(!conn.defunct, "invalid ConnectionId: already defunct");
        (conn, (), addr)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct MaybeListenerId(usize);

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

impl SocketMapAddrStateSpec for MaybeClosedConnectionId {
    type Id = MaybeClosedConnectionId;
    type SharingState = ();

    fn new((): &(), id: MaybeClosedConnectionId) -> Self {
        id
    }

    fn remove_by_id(&mut self, id: MaybeClosedConnectionId) -> RemoveResult {
        assert_eq!(self, &id);
        RemoveResult::IsLast
    }

    fn try_get_dest<'a, 'b>(
        &'b mut self,
        (): &'a (),
    ) -> Result<&'b mut Vec<MaybeClosedConnectionId>, IncompatibleError> {
        Err(IncompatibleError)
    }
}

pub(crate) trait TcpSocketHandler<I: Ip, C: TcpNonSyncContext>:
    IpDeviceIdContext<I>
{
    fn create_socket(&mut self, ctx: &mut C) -> UnboundId;

    fn bind(
        &mut self,
        ctx: &mut C,
        id: UnboundId,
        local_ip: I::Addr,
        port: Option<NonZeroU16>,
    ) -> Result<BoundId, BindError>;

    fn listen(&mut self, _ctx: &mut C, id: BoundId, backlog: NonZeroUsize) -> ListenerId;

    fn accept(
        &mut self,
        _ctx: &mut C,
        id: ListenerId,
    ) -> Result<(ConnectionId, SocketAddr<I::Addr>, C::ReturnedBuffers), AcceptError>;

    fn connect_bound(
        &mut self,
        ctx: &mut C,
        id: BoundId,
        remote: SocketAddr<I::Addr>,
        netstack_buffers: C::ProvidedBuffers,
    ) -> Result<ConnectionId, ConnectError>;

    fn connect_unbound(
        &mut self,
        ctx: &mut C,
        id: UnboundId,
        remote: SocketAddr<I::Addr>,
        netstack_buffers: C::ProvidedBuffers,
    ) -> Result<ConnectionId, ConnectError>;

    fn shutdown_conn(&mut self, ctx: &mut C, id: ConnectionId) -> Result<(), NoConnection>;
    fn close_conn(&mut self, ctx: &mut C, id: ConnectionId);
    fn remove_unbound(&mut self, id: UnboundId);
    fn remove_bound(&mut self, id: BoundId);
    fn shutdown_listener(&mut self, ctx: &mut C, id: ListenerId) -> BoundId;

    fn get_unbound_info(&self, id: UnboundId) -> UnboundInfo<Self::DeviceId>;
    fn get_bound_info(&self, id: BoundId) -> BoundInfo<I::Addr, Self::DeviceId>;
    fn get_listener_info(&self, id: ListenerId) -> BoundInfo<I::Addr, Self::DeviceId>;
    fn get_connection_info(&self, id: ConnectionId) -> ConnectionInfo<I::Addr, Self::DeviceId>;
    fn do_send(&mut self, ctx: &mut C, conn_id: MaybeClosedConnectionId);
    fn handle_timer(&mut self, ctx: &mut C, conn_id: MaybeClosedConnectionId);
}

impl<I: IpExt, C: TcpNonSyncContext, SC: TcpSyncContext<I, C>> TcpSocketHandler<I, C> for SC {
    fn create_socket(&mut self, _ctx: &mut C) -> UnboundId {
        let unbound = Unbound::default();
        UnboundId(self.with_tcp_sockets_mut(move |sockets| sockets.inactive.push(unbound)))
    }

    fn bind(
        &mut self,
        _ctx: &mut C,
        id: UnboundId,
        local_ip: I::Addr,
        port: Option<NonZeroU16>,
    ) -> Result<BoundId, BindError> {
        // TODO(https://fxbug.dev/104300): Check if local_ip is a unicast address.
        self.with_ip_transport_ctx_and_tcp_sockets_mut(
            |ip_transport_ctx, TcpSockets { port_alloc, inactive, socketmap }| {
                let port = match port {
                    None => match port_alloc.try_alloc(&local_ip, &socketmap) {
                        Some(port) => {
                            NonZeroU16::new(port).expect("ephemeral ports must be non-zero")
                        }
                        None => return Err(BindError::NoPort),
                    },
                    Some(port) => port,
                };

                let local_ip = SpecifiedAddr::new(local_ip);
                if let Some(ip) = local_ip {
                    if ip_transport_ctx.get_devices_with_assigned_addr(ip).next().is_none() {
                        return Err(BindError::NoLocalAddr);
                    }
                }

                let inactive_entry = match inactive.entry(id.into()) {
                    IdMapEntry::Vacant(_) => panic!("invalid unbound ID"),
                    IdMapEntry::Occupied(o) => o,
                };

                let Unbound { bound_device: _, buffer_sizes } = &inactive_entry.get();
                let bound_state = BoundState { buffer_sizes: buffer_sizes.clone() };
                let bound = socketmap
                    .listeners_mut()
                    .try_insert(
                        ListenerAddr {
                            ip: ListenerIpAddr { addr: local_ip, identifier: port },
                            device: None,
                        },
                        MaybeListener::Bound(bound_state),
                        // TODO(https://fxbug.dev/101596): Support sharing for TCP sockets.
                        (),
                    )
                    .map(|entry| {
                        let MaybeListenerId(x) = entry.id();
                        BoundId(x)
                    })
                    .map_err(|_: (InsertError, MaybeListener<_>, ())| BindError::Conflict)?;
                let _: Unbound<_> = inactive_entry.remove();
                Ok(bound)
            },
        )
    }

    fn listen(&mut self, _ctx: &mut C, id: BoundId, backlog: NonZeroUsize) -> ListenerId {
        let id = MaybeListenerId::from(id);
        self.with_tcp_sockets_mut(|sockets| {
            let (listener, _, _): (_, &(), &ListenerAddr<_, _, _>) =
                sockets.socketmap.listeners_mut().get_by_id_mut(&id).expect("invalid listener id");

            match listener {
                MaybeListener::Bound(BoundState { buffer_sizes }) => {
                    *listener =
                        MaybeListener::Listener(Listener::new(backlog, buffer_sizes.clone()));
                }
                MaybeListener::Listener(_) => {
                    unreachable!("invalid bound id that points to a listener entry")
                }
            }
        });

        ListenerId(id.into())
    }

    fn accept(
        &mut self,
        _ctx: &mut C,
        id: ListenerId,
    ) -> Result<(ConnectionId, SocketAddr<I::Addr>, C::ReturnedBuffers), AcceptError> {
        self.with_tcp_sockets_mut(|sockets| {
            let listener = sockets.get_listener_by_id_mut(id).expect("invalid listener id");
            let (conn_id, client_buffers) =
                listener.ready.pop_front().ok_or(AcceptError::WouldBlock)?;
            let (conn, (), conn_addr) = conn_id.get_from_socketmap_mut(&mut sockets.socketmap);
            conn.acceptor = None;
            let (remote_ip, remote_port) = conn_addr.ip.remote;
            Ok((conn_id, SocketAddr { ip: remote_ip, port: remote_port }, client_buffers))
        })
    }

    fn connect_bound(
        &mut self,
        ctx: &mut C,
        id: BoundId,
        remote: SocketAddr<I::Addr>,
        netstack_buffers: C::ProvidedBuffers,
    ) -> Result<ConnectionId, ConnectError> {
        let bound_id = MaybeListenerId::from(id);
        self.with_ip_transport_ctx_isn_generator_and_tcp_sockets_mut(
            |ip_transport_ctx, isn, sockets| {
                let (bound, (), bound_addr) =
                    sockets.socketmap.listeners().get_by_id(&bound_id).expect("invalid socket id");
                let bound = assert_matches!(bound, MaybeListener::Bound(b) => b);
                let BoundState { buffer_sizes } = bound.clone();
                let ListenerAddr { ip, device } = bound_addr;
                let ListenerIpAddr { addr: local_ip, identifier: local_port } = *ip;

                let ip_sock = ip_transport_ctx
                    .new_ip_socket(
                        ctx,
                        device.as_ref(),
                        local_ip,
                        remote.ip,
                        IpProto::Tcp.into(),
                        DefaultSendOptions,
                    )
                    .map_err(|(err, DefaultSendOptions {})| match err {
                        IpSockCreationError::Route(_) => ConnectError::NoRoute,
                    })?;

                let conn_id = connect_inner(
                    isn,
                    &mut sockets.socketmap,
                    ip_transport_ctx,
                    ctx,
                    ip_sock,
                    local_port,
                    remote.port,
                    netstack_buffers,
                    buffer_sizes.clone(),
                )?;
                let _: Option<_> = sockets.socketmap.listeners_mut().remove(&bound_id);
                Ok(conn_id)
            },
        )
    }

    fn connect_unbound(
        &mut self,
        ctx: &mut C,
        id: UnboundId,
        remote: SocketAddr<I::Addr>,
        netstack_buffers: C::ProvidedBuffers,
    ) -> Result<ConnectionId, ConnectError> {
        self.with_ip_transport_ctx_isn_generator_and_tcp_sockets_mut(
            |ip_transport_ctx, isn, sockets| {
                let ip_sock = ip_transport_ctx
                    .new_ip_socket(
                        ctx,
                        None,
                        None,
                        remote.ip,
                        IpProto::Tcp.into(),
                        DefaultSendOptions,
                    )
                    .map_err(|(err, DefaultSendOptions)| match err {
                        IpSockCreationError::Route(_) => ConnectError::NoRoute,
                    })?;

                let local_port = match sockets
                    .port_alloc
                    .try_alloc(&*ip_sock.local_ip(), &sockets.socketmap)
                {
                    Some(port) => NonZeroU16::new(port).expect("ephemeral ports must be non-zero"),
                    None => return Err(ConnectError::NoPort),
                };

                let entry = sockets.inactive.entry(id.into());
                let inactive = match entry {
                    IdMapEntry::Vacant(_v) => panic!("invalid unbound ID"),
                    IdMapEntry::Occupied(o) => o,
                };
                let Unbound { buffer_sizes, bound_device: _ } = inactive.get();

                let conn_id = connect_inner(
                    isn,
                    &mut sockets.socketmap,
                    ip_transport_ctx,
                    ctx,
                    ip_sock,
                    local_port,
                    remote.port,
                    netstack_buffers,
                    buffer_sizes.clone(),
                )?;
                let _: Unbound<_> = inactive.remove();
                Ok(conn_id)
            },
        )
    }

    fn shutdown_conn(&mut self, ctx: &mut C, id: ConnectionId) -> Result<(), NoConnection> {
        self.with_ip_transport_ctx_and_tcp_sockets_mut(|ip_transport_ctx, sockets| {
            let (conn, (), addr) = id.get_from_socketmap_mut(&mut sockets.socketmap);
            match conn.state.close() {
                Ok(()) => Ok(do_send_inner(id.into(), conn, addr, ip_transport_ctx, ctx)),
                Err(CloseError::NoConnection) => Err(NoConnection),
                Err(CloseError::Closing) => Ok(()),
            }
        })
    }

    fn close_conn(&mut self, ctx: &mut C, id: ConnectionId) {
        self.with_ip_transport_ctx_and_tcp_sockets_mut(|ip_transport_ctx, sockets| {
            let (conn, (), addr) = id.get_from_socketmap_mut(&mut sockets.socketmap);
            conn.defunct = true;
            let already_closed = match conn.state.close() {
                Err(CloseError::NoConnection) => true,
                Err(CloseError::Closing) => return,
                Ok(()) => matches!(conn.state, State::Closed(_)),
            };
            if already_closed {
                assert_matches!(sockets.socketmap.conns_mut().remove(&id.into()), Some(_));
                let _: Option<_> = ctx.cancel_timer(TimerId(id.into(), I::VERSION));
                return;
            }
            do_send_inner(id.into(), conn, addr, ip_transport_ctx, ctx)
        })
    }

    fn remove_unbound(&mut self, id: UnboundId) {
        self.with_tcp_sockets_mut(|TcpSockets { socketmap: _, inactive, port_alloc: _ }| {
            assert_matches!(inactive.remove(id.into()), Some(_));
        });
    }

    fn remove_bound(&mut self, id: BoundId) {
        self.with_tcp_sockets_mut(|TcpSockets { socketmap, inactive: _, port_alloc: _ }| {
            assert_matches!(socketmap.listeners_mut().remove(&id.into()), Some(_));
        });
    }

    fn shutdown_listener(&mut self, ctx: &mut C, id: ListenerId) -> BoundId {
        self.with_ip_transport_ctx_and_tcp_sockets_mut(
            |ip_transport_ctx, TcpSockets { socketmap, inactive: _, port_alloc: _ }| {
                let (maybe_listener, (), _addr) = socketmap
                    .listeners_mut()
                    .get_by_id_mut(&id.into())
                    .expect("invalid listener ID");
                let replacement = MaybeListener::Bound(BoundState {
                    buffer_sizes: maybe_listener.buffer_sizes(),
                });
                let Listener { backlog: _, pending, ready, buffer_sizes: _ } = assert_matches!(
                    core::mem::replace(maybe_listener, replacement),
                    MaybeListener::Listener(listener) => listener);
                for conn_id in pending.into_iter().chain(
                    ready
                        .into_iter()
                        .map(|(conn_id, _passive_open): (_, C::ReturnedBuffers)| conn_id),
                ) {
                    let _: Option<C::Instant> =
                        ctx.cancel_timer(TimerId(conn_id.into(), I::VERSION));
                    let (mut conn, (), conn_addr) =
                        socketmap.conns_mut().remove(&conn_id.into()).unwrap();
                    if let Some(reset) = conn.state.abort() {
                        let ConnAddr { ip, device: _ } = conn_addr;
                        let ser = tcp_serialize_segment(reset, ip);
                        ip_transport_ctx
                            .send_ip_packet(ctx, &conn.ip_sock, ser, None)
                            .unwrap_or_else(|(body, err)| {
                                log::debug!(
                                    "failed to reset connection to {:?}, body: {:?}, err: {:?}",
                                    ip,
                                    body,
                                    err
                                )
                            });
                    }
                }
                BoundId(id.into())
            },
        )
    }

    fn get_unbound_info(&self, id: UnboundId) -> UnboundInfo<SC::DeviceId> {
        self.with_tcp_sockets(|sockets| {
            let TcpSockets { socketmap: _, inactive, port_alloc: _ } = sockets;
            inactive.get(id.into()).expect("invalid unbound ID").into()
        })
    }

    fn get_bound_info(&self, id: BoundId) -> BoundInfo<I::Addr, SC::DeviceId> {
        self.with_tcp_sockets(|sockets| {
            let (bound, (), bound_addr) =
                sockets.socketmap.listeners().get_by_id(&id.into()).expect("invalid bound ID");
            assert_matches!(bound, MaybeListener::Bound(_));
            bound_addr.clone()
        })
        .into()
    }

    fn get_listener_info(&self, id: ListenerId) -> BoundInfo<I::Addr, SC::DeviceId> {
        self.with_tcp_sockets(|sockets| {
            let (listener, (), addr) =
                sockets.socketmap.listeners().get_by_id(&id.into()).expect("invalid listener ID");
            assert_matches!(listener, MaybeListener::Listener(_));
            addr.clone()
        })
        .into()
    }

    fn get_connection_info(&self, id: ConnectionId) -> ConnectionInfo<I::Addr, SC::DeviceId> {
        self.with_tcp_sockets(|sockets| {
            let (_conn, (), addr) = id.get_from_socketmap(&sockets.socketmap);
            addr.clone()
        })
        .into()
    }

    fn do_send(&mut self, ctx: &mut C, conn_id: MaybeClosedConnectionId) {
        self.with_ip_transport_ctx_and_tcp_sockets_mut(|ip_transport_ctx, sockets| {
            if let Some((conn, (), addr)) = sockets.socketmap.conns_mut().get_by_id_mut(&conn_id) {
                do_send_inner(conn_id, conn, addr, ip_transport_ctx, ctx);
            }
        })
    }

    fn handle_timer(&mut self, ctx: &mut C, conn_id: MaybeClosedConnectionId) {
        self.with_ip_transport_ctx_and_tcp_sockets_mut(|ip_transport_ctx, sockets| {
            if let Some((conn, (), addr)) = sockets.socketmap.conns_mut().get_by_id_mut(&conn_id) {
                do_send_inner(conn_id, conn, addr, ip_transport_ctx, ctx);
                if conn.defunct && matches!(conn.state, State::Closed(_)) {
                    assert_matches!(sockets.socketmap.conns_mut().remove(&conn_id), Some(_));
                    let _: Option<_> = ctx.cancel_timer(TimerId(conn_id, I::VERSION));
                }
            }
        })
    }
}

fn do_send_inner<I, SC, C>(
    conn_id: MaybeClosedConnectionId,
    conn: &mut Connection<
        I,
        SC::DeviceId,
        C::Instant,
        C::ReceiveBuffer,
        C::SendBuffer,
        C::ProvidedBuffers,
    >,
    addr: &ConnAddr<I::Addr, SC::DeviceId, NonZeroU16, NonZeroU16>,
    ip_transport_ctx: &mut SC,
    ctx: &mut C,
) where
    I: IpExt,
    C: TcpNonSyncContext,
    SC: BufferTransportIpContext<I, C, Buf<Vec<u8>>>,
{
    if let Some(seg) = conn.state.poll_send(DEFAULT_MAXIMUM_SEGMENT_SIZE, ctx.now()) {
        let ser = tcp_serialize_segment(seg, addr.ip.clone());
        ip_transport_ctx.send_ip_packet(ctx, &conn.ip_sock, ser, None).unwrap_or_else(
            |(body, err)| {
                // Currently there are a few call sites to `do_send_inner` and they
                // don't really care about the error, with Rust's strict
                // `unused_result` lint, not returning an error that no one
                // would care makes the code less cumbersome to write. So We do
                // not return the error to caller but just log it instead. If
                // we find a case where the caller is interested in the error,
                // then we can always come back and change this.
                log::debug!(
                    "failed to send an ip packet on {:?}, body: {:?}, err: {:?}",
                    conn_id,
                    body,
                    err
                )
            },
        );
    }

    if let Some(instant) = conn.state.poll_send_at() {
        let _: Option<_> = ctx.schedule_timer_instant(instant, TimerId(conn_id, I::VERSION));
    }
}

macro_rules! with_ip_version {
    (Ip, $i:ident, $method:ident($($arg:tt)*)) => {
        match $i::VERSION {
            net_types::ip::IpVersion::V4 => TcpSocketHandler::<Ipv4, _>::$method($($arg)*),
            net_types::ip::IpVersion::V6 => TcpSocketHandler::<Ipv6, _>::$method($($arg)*),
        }
    };

    (Address, $a:ident, $method:ident($($arg:tt)*)) => {
        match $a.into() {
            net_types::ip::IpAddr::V4($a) => TcpSocketHandler::<Ipv4, _>::$method($($arg)*),
            net_types::ip::IpAddr::V6($a) => TcpSocketHandler::<Ipv6, _>::$method($($arg)*),
        }
    };
}

/// Creates a new socket in unbound state.
pub fn create_socket<I, C>(mut sync_ctx: &SyncCtx<C>, ctx: &mut C) -> UnboundId
where
    I: IpExt,
    C: NonSyncContext,
{
    with_ip_version!(Ip, I, create_socket(&mut sync_ctx, ctx))
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
pub fn bind<I, C>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UnboundId,
    local_ip: I::Addr,
    port: Option<NonZeroU16>,
) -> Result<BoundId, BindError>
where
    I: IpExt,
    C: NonSyncContext,
{
    with_ip_version!(Address, local_ip, bind(&mut sync_ctx, ctx, id, local_ip, port))
}

/// Listens on an already bound socket.
pub fn listen<I, C>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: BoundId,
    backlog: NonZeroUsize,
) -> ListenerId
where
    I: IpExt,
    C: NonSyncContext,
{
    with_ip_version!(Ip, I, listen(&mut sync_ctx, ctx, id, backlog))
}

/// Possible errors for accept operation.
#[derive(Debug)]
pub enum AcceptError {
    /// There is no established socket currently.
    WouldBlock,
}

/// Possible error for calling `shutdown` on a not-yet connected socket.
#[derive(Debug)]
pub struct NoConnection;

/// Accepts an established socket from the queue of a listener socket.
pub fn accept<I: Ip, C>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: ListenerId,
) -> Result<(ConnectionId, SocketAddr<I::Addr>, C::ReturnedBuffers), AcceptError>
where
    C: NonSyncContext,
{
    I::map_ip::<_, Result<_, _>>(
        IpInv((&mut sync_ctx, ctx, id)),
        |IpInv((sync_ctx, ctx, id))| {
            TcpSocketHandler::<Ipv4, _>::accept(sync_ctx, ctx, id)
                .map(|(a, b, c)| (IpInv(a), b, IpInv(c)))
                .map_err(IpInv)
        },
        |IpInv((sync_ctx, ctx, id))| {
            TcpSocketHandler::<Ipv6, _>::accept(sync_ctx, ctx, id)
                .map(|(a, b, c)| (IpInv(a), b, IpInv(c)))
                .map_err(IpInv)
        },
    )
    .map(|(IpInv(a), b, IpInv(c))| (a, b, c))
    .map_err(|IpInv(e)| e)
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
pub fn connect_bound<I, C>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: BoundId,
    remote: SocketAddr<I::Addr>,
    netstack_buffers: C::ProvidedBuffers,
) -> Result<ConnectionId, ConnectError>
where
    I: IpExt,
    C: NonSyncContext,
{
    with_ip_version!(
        Address,
        remote,
        connect_bound(&mut sync_ctx, ctx, id, remote, netstack_buffers)
    )
}

/// Connects a socket that is in unbound state.
pub fn connect_unbound<I, C>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: UnboundId,
    remote: SocketAddr<I::Addr>,
    netstack_buffers: C::ProvidedBuffers,
) -> Result<ConnectionId, ConnectError>
where
    I: IpExt,
    C: NonSyncContext,
{
    with_ip_version!(
        Address,
        remote,
        connect_unbound(&mut sync_ctx, ctx, id, remote, netstack_buffers)
    )
}

fn connect_inner<I, SC, C>(
    isn: &IsnGenerator<C::Instant>,
    socketmap: &mut BoundSocketMap<IpPortSpec<I, SC::DeviceId>, TcpSocketSpec<I, SC::DeviceId, C>>,
    ip_transport_ctx: &mut SC,
    ctx: &mut C,
    ip_sock: IpSock<I, SC::DeviceId, DefaultSendOptions>,
    local_port: NonZeroU16,
    remote_port: NonZeroU16,
    netstack_buffers: C::ProvidedBuffers,
    buffer_sizes: BufferSizes,
) -> Result<ConnectionId, ConnectError>
where
    I: IpExt,
    C: TcpNonSyncContext,
    SC: BufferTransportIpContext<I, C, Buf<Vec<u8>>>,
{
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
    let (syn_sent, syn) = Closed::<Initial>::connect(isn, now, netstack_buffers, buffer_sizes);
    let state = State::SynSent(syn_sent);
    let poll_send_at = state.poll_send_at().expect("no retrans timer");
    let conn_id = socketmap
        .conns_mut()
        .try_insert(
            conn_addr.clone(),
            Connection { acceptor: None, state, ip_sock: ip_sock.clone(), defunct: false },
            // TODO(https://fxbug.dev/101596): Support sharing for TCP sockets.
            (),
        )
        .expect("failed to insert connection")
        .id();

    ip_transport_ctx
        .send_ip_packet(ctx, &ip_sock, tcp_serialize_segment(syn, conn_addr.ip), None)
        .map_err(|(body, err)| {
            warn!("tcp: failed to send ip packet {:?}: {:?}", body, err);
            assert_matches!(socketmap.conns_mut().remove(&conn_id), Some(_));
            ConnectError::NoRoute
        })?;
    assert_eq!(ctx.schedule_timer_instant(poll_send_at, TimerId(conn_id, I::VERSION)), None);
    // This conversion Ok because `conn_id` is newly created; No one should
    // have called close on it.
    let MaybeClosedConnectionId(id) = conn_id;
    Ok(ConnectionId(id))
}

/// Closes the connection. The user has promised that they will not use `id`
/// again, we can reclaim the connection after the connection becomes `Closed`.
pub fn close_conn<I, C>(mut sync_ctx: &SyncCtx<C>, ctx: &mut C, id: ConnectionId)
where
    I: IpExt,
    C: NonSyncContext,
{
    with_ip_version!(Ip, I, close_conn(&mut sync_ctx, ctx, id))
}

/// Shuts down the write-half of the connection. Calling this function signals
/// the other side of the connection that we will not be sending anything over
/// the connection; The connection will still stay in the socketmap even after
/// reaching `Closed` state. The user needs to call `close_conn` in order to
/// remove it.
pub fn shutdown_conn<I, C>(
    mut sync_ctx: &SyncCtx<C>,
    ctx: &mut C,
    id: ConnectionId,
) -> Result<(), NoConnection>
where
    I: IpExt,
    C: NonSyncContext,
{
    with_ip_version!(Ip, I, shutdown_conn(&mut sync_ctx, ctx, id))
}

/// Removes an unbound socket.
pub fn remove_unbound<I, C>(mut sync_ctx: &SyncCtx<C>, id: UnboundId)
where
    I: IpExt,
    C: NonSyncContext,
{
    with_ip_version!(Ip, I, remove_unbound(&mut sync_ctx, id))
}

/// Removes a bound socket.
pub fn remove_bound<I, C>(mut sync_ctx: &SyncCtx<C>, id: BoundId)
where
    I: IpExt,
    C: NonSyncContext,
{
    with_ip_version!(Ip, I, remove_bound(&mut sync_ctx, id))
}

/// Shuts down a listener socket.
///
/// The socket remains in the socket map as a bound socket, taking the port
/// that the socket has been using. Returns the id of that bound socket.
pub fn shutdown_listener<I, C>(mut sync_ctx: &SyncCtx<C>, ctx: &mut C, id: ListenerId) -> BoundId
where
    I: IpExt,
    C: NonSyncContext,
{
    with_ip_version!(Ip, I, shutdown_listener(&mut sync_ctx, ctx, id))
}

/// Information about an unbound socket.
#[derive(Clone, Debug, Eq, PartialEq, GenericOverIp)]
pub struct UnboundInfo<D> {
    /// The device the socket will be bound to.
    pub device: Option<D>,
}

/// Information about a bound socket's address.
#[derive(Clone, Debug, Eq, PartialEq, GenericOverIp)]
pub struct BoundInfo<A: IpAddress, D> {
    /// The IP address the socket is bound to, or `None` for all local IPs.
    pub addr: Option<SpecifiedAddr<A>>,
    /// The port number the socket is bound to.
    pub port: NonZeroU16,
    /// The device the socket is bound to.
    pub device: Option<D>,
}

/// Information about a connected socket's address.
#[derive(Clone, Debug, Eq, PartialEq, GenericOverIp)]
pub struct ConnectionInfo<A: IpAddress, D> {
    /// The local address the socket is bound to.
    pub local_addr: SocketAddr<A>,
    /// The remote address the socket is connected to.
    pub remote_addr: SocketAddr<A>,
    /// The device the socket is bound to.
    pub device: Option<D>,
}

impl<D: Clone> From<&'_ Unbound<D>> for UnboundInfo<D> {
    fn from(unbound: &Unbound<D>) -> Self {
        let Unbound { bound_device: device, buffer_sizes: _ } = unbound;
        Self { device: device.clone() }
    }
}

impl<A: IpAddress, D> From<ListenerAddr<A, D, NonZeroU16>> for BoundInfo<A, D> {
    fn from(addr: ListenerAddr<A, D, NonZeroU16>) -> Self {
        let ListenerAddr { ip: ListenerIpAddr { addr, identifier }, device } = addr;
        BoundInfo { addr, port: identifier, device }
    }
}

impl<A: IpAddress, D> From<ConnAddr<A, D, NonZeroU16, NonZeroU16>> for ConnectionInfo<A, D> {
    fn from(addr: ConnAddr<A, D, NonZeroU16, NonZeroU16>) -> Self {
        let ConnAddr { ip: ConnIpAddr { local, remote }, device } = addr;
        let convert = |(ip, port)| SocketAddr { ip, port };
        Self { local_addr: convert(local), remote_addr: convert(remote), device }
    }
}

/// Get information for unbound TCP socket.
pub fn get_unbound_info<I: Ip, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    id: UnboundId,
) -> UnboundInfo<DeviceId<C::Instant>> {
    I::map_ip(
        IpInv((&mut sync_ctx, id)),
        |IpInv((sync_ctx, id))| TcpSocketHandler::<Ipv4, _>::get_unbound_info(sync_ctx, id),
        |IpInv((sync_ctx, id))| TcpSocketHandler::<Ipv6, _>::get_unbound_info(sync_ctx, id),
    )
}

/// Get information for bound TCP socket.
pub fn get_bound_info<I: Ip, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    id: BoundId,
) -> BoundInfo<I::Addr, DeviceId<C::Instant>> {
    I::map_ip(
        IpInv((&mut sync_ctx, id)),
        |IpInv((sync_ctx, id))| TcpSocketHandler::<Ipv4, _>::get_bound_info(sync_ctx, id),
        |IpInv((sync_ctx, id))| TcpSocketHandler::<Ipv6, _>::get_bound_info(sync_ctx, id),
    )
}

/// Get information for listener TCP socket.
pub fn get_listener_info<I: Ip, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    id: ListenerId,
) -> BoundInfo<I::Addr, DeviceId<C::Instant>> {
    I::map_ip(
        IpInv((&mut sync_ctx, id)),
        |IpInv((sync_ctx, id))| TcpSocketHandler::<Ipv4, _>::get_listener_info(sync_ctx, id),
        |IpInv((sync_ctx, id))| TcpSocketHandler::<Ipv6, _>::get_listener_info(sync_ctx, id),
    )
}

/// Get information for connection TCP socket.
pub fn get_connection_info<I: Ip, C: NonSyncContext>(
    mut sync_ctx: &SyncCtx<C>,
    id: ConnectionId,
) -> ConnectionInfo<I::Addr, DeviceId<C::Instant>> {
    I::map_ip(
        IpInv((&mut sync_ctx, id)),
        |IpInv((sync_ctx, id))| TcpSocketHandler::<Ipv4, _>::get_connection_info(sync_ctx, id),
        |IpInv((sync_ctx, id))| TcpSocketHandler::<Ipv6, _>::get_connection_info(sync_ctx, id),
    )
}

/// Call this function whenever a socket can push out more data. That means either:
///
/// - A retransmission timer fires.
/// - An ack received from peer so that our send window is enlarged.
/// - The user puts data into the buffer and we are notified.
pub fn do_send<I, C>(mut sync_ctx: &SyncCtx<C>, ctx: &mut C, conn_id: MaybeClosedConnectionId)
where
    I: IpExt,
    C: NonSyncContext,
{
    with_ip_version!(Ip, I, do_send(&mut sync_ctx, ctx, conn_id))
}

pub(crate) fn handle_timer<SC, C>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    TimerId(conn_id, version): TimerId,
) where
    C: TcpNonSyncContext,
    SC: TcpSyncContext<Ipv4, C> + TcpSyncContext<Ipv6, C>,
{
    match version {
        IpVersion::V4 => TcpSocketHandler::<Ipv4, _>::handle_timer(sync_ctx, ctx, conn_id),
        IpVersion::V6 => TcpSocketHandler::<Ipv6, _>::handle_timer(sync_ctx, ctx, conn_id),
    }
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

impl From<usize> for MaybeClosedConnectionId {
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

impl Into<usize> for MaybeClosedConnectionId {
    fn into(self) -> usize {
        let Self(x) = self;
        x
    }
}

impl From<ConnectionId> for MaybeClosedConnectionId {
    fn from(ConnectionId(id): ConnectionId) -> Self {
        Self(id)
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
    use ip_test_macro::ip_test;
    use net_types::ip::{AddrSubnet, Ip, Ipv4, Ipv6, Ipv6SourceAddr};
    use packet::ParseBuffer as _;
    use packet_formats::tcp::{TcpParseArgs, TcpSegment};
    use rand::Rng as _;
    use test_case::test_case;

    use crate::{
        context::testutil::{
            FakeCtxWithSyncCtx, FakeFrameCtx, FakeInstant, FakeNetwork, FakeNetworkContext,
            FakeNonSyncCtx, FakeSyncCtx, InstantAndData, PendingFrameData, StepResult,
            WrappedFakeSyncCtx,
        },
        ip::{
            device::state::{
                AddrConfig, AddressState, IpDeviceState, IpDeviceStateIpExt, Ipv6AddressEntry,
            },
            socket::testutil::{FakeBufferIpSocketCtx, FakeIpSocketCtx},
            testutil::FakeDeviceId,
            BufferIpTransportContext as _, SendIpPacketMeta,
        },
        testutil::{new_rng, run_with_many_seeds, set_logger_for_test, FakeCryptoRng, TestIpExt},
        transport::tcp::{
            buffer::{Buffer, RingBuffer, SendPayload},
            segment::Payload,
            UserError,
        },
    };

    use super::*;

    trait TcpTestIpExt: IpExt + TestIpExt + IpDeviceStateIpExt {
        fn recv_src_addr(addr: Self::Addr) -> Self::RecvSrcAddr;

        fn new_device_state(addr: Self::Addr, prefix: u8) -> IpDeviceState<FakeInstant, Self>;
    }

    type FakeBufferIpTransportCtx<I> = FakeSyncCtx<
        FakeBufferIpSocketCtx<I, FakeDeviceId>,
        SendIpPacketMeta<I, FakeDeviceId, SpecifiedAddr<<I as Ip>::Addr>>,
        FakeDeviceId,
    >;

    struct FakeTcpState<I: TcpTestIpExt> {
        isn_generator: IsnGenerator<FakeInstant>,
        sockets: TcpSockets<I, FakeDeviceId, TcpNonSyncCtx>,
    }

    type TcpSyncCtx<I> = WrappedFakeSyncCtx<
        FakeTcpState<I>,
        FakeBufferIpSocketCtx<I, FakeDeviceId>,
        SendIpPacketMeta<I, FakeDeviceId, SpecifiedAddr<<I as Ip>::Addr>>,
        FakeDeviceId,
    >;

    type TcpCtx<I> = FakeCtxWithSyncCtx<TcpSyncCtx<I>, TimerId, (), ()>;

    impl<I: TcpTestIpExt>
        AsMut<FakeFrameCtx<SendIpPacketMeta<I, FakeDeviceId, SpecifiedAddr<<I as Ip>::Addr>>>>
        for TcpCtx<I>
    {
        fn as_mut(
            &mut self,
        ) -> &mut FakeFrameCtx<SendIpPacketMeta<I, FakeDeviceId, SpecifiedAddr<<I as Ip>::Addr>>>
        {
            self.sync_ctx.inner.as_mut()
        }
    }

    impl<I: TcpTestIpExt> FakeNetworkContext for TcpCtx<I> {
        type TimerId = TimerId;
        type SendMeta = SendIpPacketMeta<I, FakeDeviceId, SpecifiedAddr<<I as Ip>::Addr>>;
    }

    type TcpNonSyncCtx = FakeNonSyncCtx<TimerId, (), ()>;

    impl Buffer for Rc<RefCell<RingBuffer>> {
        fn len(&self) -> usize {
            self.borrow().len()
        }
    }

    impl ReceiveBuffer for Rc<RefCell<RingBuffer>> {
        fn cap(&self) -> usize {
            self.borrow().cap()
        }

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

    #[derive(Clone, Debug, Default, Eq, PartialEq)]
    pub(crate) struct ClientBuffers {
        receive: Rc<RefCell<RingBuffer>>,
        send: Rc<RefCell<Vec<u8>>>,
    }

    impl TcpNonSyncContext for TcpNonSyncCtx {
        type ReceiveBuffer = Rc<RefCell<RingBuffer>>;
        type SendBuffer = TestSendBuffer;
        type ReturnedBuffers = ClientBuffers;
        type ProvidedBuffers = ClientBuffers;

        fn on_new_connection(&mut self, _listener: ListenerId) {}
        fn new_passive_open_buffers(
            buffer_sizes: BufferSizes,
        ) -> (Self::ReceiveBuffer, Self::SendBuffer, Self::ReturnedBuffers) {
            let BufferSizes {} = buffer_sizes;
            let client = ClientBuffers::default();
            (
                Rc::clone(&client.receive),
                TestSendBuffer(Rc::clone(&client.send), RingBuffer::default()),
                client,
            )
        }
    }

    impl IntoBuffers<Rc<RefCell<RingBuffer>>, TestSendBuffer> for ClientBuffers {
        fn into_buffers(
            self,
            buffer_sizes: BufferSizes,
        ) -> (Rc<RefCell<RingBuffer>>, TestSendBuffer) {
            let Self { receive, send } = self;
            let BufferSizes {} = buffer_sizes;
            (receive, TestSendBuffer(send, Default::default()))
        }
    }

    impl<I: TcpTestIpExt> TcpSyncContext<I, TcpNonSyncCtx> for TcpSyncCtx<I> {
        type IpTransportCtx = FakeBufferIpTransportCtx<I>;

        fn with_ip_transport_ctx_isn_generator_and_tcp_sockets_mut<
            O,
            F: FnOnce(
                &mut FakeBufferIpTransportCtx<I>,
                &IsnGenerator<FakeInstant>,
                &mut TcpSockets<I, FakeDeviceId, TcpNonSyncCtx>,
            ) -> O,
        >(
            &mut self,
            cb: F,
        ) -> O {
            let WrappedFakeSyncCtx {
                outer: FakeTcpState { isn_generator, sockets },
                inner: ip_transport_ctx,
            } = self;
            cb(ip_transport_ctx, isn_generator, sockets)
        }

        fn with_tcp_sockets<O, F: FnOnce(&TcpSockets<I, FakeDeviceId, TcpNonSyncCtx>) -> O>(
            &self,
            cb: F,
        ) -> O {
            let WrappedFakeSyncCtx { outer: FakeTcpState { isn_generator: _, sockets }, inner: _ } =
                self;
            cb(sockets)
        }
    }

    impl<I: TcpTestIpExt> TcpSyncCtx<I> {
        fn new(addr: SpecifiedAddr<I::Addr>, peer: SpecifiedAddr<I::Addr>, prefix: u8) -> Self {
            Self::with_inner_and_outer_state(
                FakeBufferIpSocketCtx::with_ctx(FakeIpSocketCtx::<I, _>::with_devices_state(
                    core::iter::once((
                        FakeDeviceId,
                        I::new_device_state(*addr, prefix),
                        alloc::vec![peer],
                    )),
                )),
                FakeTcpState {
                    isn_generator: Default::default(),
                    sockets: TcpSockets {
                        inactive: IdMap::new(),
                        socketmap: BoundSocketMap::default(),
                        port_alloc: PortAlloc::new(&mut FakeCryptoRng::new_xorshift(0)),
                    },
                },
            )
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

        fn new_device_state(addr: Self::Addr, prefix: u8) -> IpDeviceState<FakeInstant, Self> {
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

        fn new_device_state(addr: Self::Addr, prefix: u8) -> IpDeviceState<FakeInstant, Self> {
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

    type TcpTestNetwork<I> = FakeNetwork<
        &'static str,
        SendIpPacketMeta<I, FakeDeviceId, SpecifiedAddr<<I as Ip>::Addr>>,
        TcpCtx<I>,
        fn(
            &'static str,
            SendIpPacketMeta<I, FakeDeviceId, SpecifiedAddr<<I as Ip>::Addr>>,
        ) -> Vec<(
            &'static str,
            SendIpPacketMeta<I, FakeDeviceId, SpecifiedAddr<<I as Ip>::Addr>>,
            Option<core::time::Duration>,
        )>,
    >;

    fn new_test_net<I: TcpTestIpExt>() -> TcpTestNetwork<I> {
        FakeNetwork::new(
            [
                (
                    LOCAL,
                    TcpCtx::with_sync_ctx(TcpSyncCtx::new(
                        I::FAKE_CONFIG.local_ip,
                        I::FAKE_CONFIG.remote_ip,
                        I::FAKE_CONFIG.subnet.prefix(),
                    )),
                ),
                (
                    REMOTE,
                    TcpCtx::with_sync_ctx(TcpSyncCtx::new(
                        I::FAKE_CONFIG.remote_ip,
                        I::FAKE_CONFIG.local_ip,
                        I::FAKE_CONFIG.subnet.prefix(),
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
        meta: SendIpPacketMeta<I, FakeDeviceId, SpecifiedAddr<I::Addr>>,
        buffer: Buf<Vec<u8>>,
    ) {
        TcpIpTransportContext::receive_ip_packet(
            sync_ctx,
            non_sync_ctx,
            &FakeDeviceId,
            I::recv_src_addr(*meta.src_ip),
            meta.dst_ip,
            buffer,
        )
        .expect("failed to deliver bytes");
    }

    fn handle_timer<I: Ip + TcpTestIpExt>(
        ctx: &mut TcpCtx<I>,
        _: &mut (),
        TimerId(conn_id, version): TimerId,
    ) {
        assert_eq!(I::VERSION, version);
        let FakeCtxWithSyncCtx { sync_ctx, non_sync_ctx } = ctx;
        TcpSocketHandler::<I, _>::handle_timer(sync_ctx, non_sync_ctx, conn_id)
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
        seed: u128,
        drop_rate: f64,
    ) -> (TcpTestNetwork<I>, ConnectionId, ConnectionId) {
        let mut net = new_test_net::<I>();
        let mut rng = new_rng(seed);

        let mut maybe_drop_frame =
            |ctx: &mut TcpCtx<I>,
             meta: SendIpPacketMeta<I, FakeDeviceId, SpecifiedAddr<<I as Ip>::Addr>>,
             buffer: Buf<Vec<u8>>| {
                let x: f64 = rng.gen();
                if x > drop_rate {
                    handle_frame(ctx, meta, buffer);
                }
            };

        let backlog = NonZeroUsize::new(1).unwrap();
        let server = net.with_context(REMOTE, |TcpCtx { sync_ctx, non_sync_ctx }| {
            let conn = TcpSocketHandler::create_socket(sync_ctx, non_sync_ctx);
            let bound =
                TcpSocketHandler::bind(sync_ctx, non_sync_ctx, conn, listen_addr, Some(PORT_1))
                    .expect("failed to bind the server socket");
            TcpSocketHandler::listen(sync_ctx, non_sync_ctx, bound, backlog)
        });

        let client_ends = ClientBuffers::default();
        let client = net.with_context(LOCAL, |TcpCtx { sync_ctx, non_sync_ctx }| {
            let conn = TcpSocketHandler::create_socket(sync_ctx, non_sync_ctx);
            if bind_client {
                let conn = TcpSocketHandler::bind(
                    sync_ctx,
                    non_sync_ctx,
                    conn,
                    *I::FAKE_CONFIG.local_ip,
                    Some(PORT_1),
                )
                .expect("failed to bind the client socket");
                TcpSocketHandler::connect_bound(
                    sync_ctx,
                    non_sync_ctx,
                    conn,
                    SocketAddr { ip: I::FAKE_CONFIG.remote_ip, port: PORT_1 },
                    client_ends.clone(),
                )
                .expect("failed to connect")
            } else {
                TcpSocketHandler::connect_unbound(
                    sync_ctx,
                    non_sync_ctx,
                    conn,
                    SocketAddr { ip: I::FAKE_CONFIG.remote_ip, port: PORT_1 },
                    client_ends.clone(),
                )
                .expect("failed to connect")
            }
        });
        // If drop rate is 0, the SYN is guaranteed to be delivered, so we can
        // look at the SYN queue deterministically.
        if drop_rate == 0.0 {
            // Step once for the SYN packet to be sent.
            let _: StepResult = net.step(handle_frame, handle_timer);
            // The listener should create a pending socket.
            assert_matches!(
                net.sync_ctx(REMOTE).outer.sockets.get_listener_by_id_mut(server),
                Some(Listener { backlog: _, ready, pending, buffer_sizes: _ }) => {
                    assert_eq!(ready.len(), 0);
                    assert_eq!(pending.len(), 1);
                }
            );
            // The handshake is not done, calling accept here should not succeed.
            net.with_context(REMOTE, |TcpCtx { sync_ctx, non_sync_ctx }| {
                assert_matches!(
                    TcpSocketHandler::accept(sync_ctx, non_sync_ctx, server),
                    Err(AcceptError::WouldBlock)
                );
            });
        }

        // Step the test network until the handshake is done.
        net.run_until_idle(&mut maybe_drop_frame, handle_timer);
        let (accepted, addr, accepted_ends) =
            net.with_context(REMOTE, |TcpCtx { sync_ctx, non_sync_ctx }| {
                TcpSocketHandler::accept(sync_ctx, non_sync_ctx, server).expect("failed to accept")
            });
        if bind_client {
            assert_eq!(addr, SocketAddr { ip: I::FAKE_CONFIG.local_ip, port: PORT_1 });
        } else {
            assert_eq!(addr.ip, I::FAKE_CONFIG.local_ip);
        }

        let mut assert_connected = |name: &'static str, conn_id: ConnectionId| {
            let (conn, (), _): (_, _, &ConnAddr<_, _, _, _>) =
                conn_id.get_from_socketmap(&net.sync_ctx(name).outer.sockets.socketmap);
            assert_matches!(
                conn,
                Connection {
                    acceptor: None,
                    state: State::Established(_),
                    ip_sock: _,
                    defunct: false
                }
            )
        };

        assert_connected(LOCAL, client);
        assert_connected(REMOTE, accepted);

        let ClientBuffers { send: client_snd_end, receive: client_rcv_end } = client_ends;
        let ClientBuffers { send: accepted_snd_end, receive: accepted_rcv_end } = accepted_ends;
        for snd_end in [client_snd_end, accepted_snd_end] {
            snd_end.borrow_mut().extend_from_slice(b"Hello");
        }

        for (c, id) in [(LOCAL, client), (REMOTE, accepted)] {
            net.with_context(c, |TcpCtx { sync_ctx, non_sync_ctx }| {
                TcpSocketHandler::<I, _>::do_send(sync_ctx, non_sync_ctx, id.into())
            })
        }
        net.run_until_idle(&mut maybe_drop_frame, handle_timer);

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
            net.sync_ctx(REMOTE).outer.sockets.get_listener_by_id_mut(server),
            Some(&mut Listener::new(backlog, BufferSizes::default())),
        );

        (net, client, accepted)
    }

    #[ip_test]
    fn bind_listen_connect_accept<I: Ip + TcpTestIpExt>() {
        set_logger_for_test();
        for bind_client in [true, false] {
            for listen_addr in [I::UNSPECIFIED_ADDRESS, *I::FAKE_CONFIG.remote_ip] {
                let (_net, _client, _accepted) =
                    bind_listen_connect_accept_inner::<I>(listen_addr, bind_client, 0, 0.0);
            }
        }
    }

    #[ip_test]
    #[test_case(*<I as TestIpExt>::FAKE_CONFIG.local_ip; "same addr")]
    #[test_case(I::UNSPECIFIED_ADDRESS; "any addr")]
    fn bind_conflict<I: Ip + TcpTestIpExt>(conflict_addr: I::Addr) {
        set_logger_for_test();
        let TcpCtx { mut sync_ctx, mut non_sync_ctx } =
            TcpCtx::<I>::with_sync_ctx(TcpSyncCtx::new(
                I::FAKE_CONFIG.local_ip,
                I::FAKE_CONFIG.local_ip,
                I::FAKE_CONFIG.subnet.prefix(),
            ));
        let s1 = TcpSocketHandler::create_socket(&mut sync_ctx, &mut non_sync_ctx);
        let s2 = TcpSocketHandler::create_socket(&mut sync_ctx, &mut non_sync_ctx);

        let _b1 = TcpSocketHandler::bind(
            &mut sync_ctx,
            &mut non_sync_ctx,
            s1,
            *I::FAKE_CONFIG.local_ip,
            Some(PORT_1),
        )
        .expect("first bind should succeed");
        assert_matches!(
            TcpSocketHandler::bind(
                &mut sync_ctx,
                &mut non_sync_ctx,
                s2,
                conflict_addr,
                Some(PORT_1)
            ),
            Err(BindError::Conflict)
        );
        let _b2 = TcpSocketHandler::bind(
            &mut sync_ctx,
            &mut non_sync_ctx,
            s2,
            conflict_addr,
            Some(PORT_2),
        )
        .expect("able to rebind to a free address");
    }

    #[ip_test]
    fn bind_to_non_existent_address<I: Ip + TcpTestIpExt>() {
        let TcpCtx { mut sync_ctx, mut non_sync_ctx } =
            TcpCtx::<I>::with_sync_ctx(TcpSyncCtx::new(
                I::FAKE_CONFIG.local_ip,
                I::FAKE_CONFIG.local_ip,
                I::FAKE_CONFIG.subnet.prefix(),
            ));
        let unbound = TcpSocketHandler::create_socket(&mut sync_ctx, &mut non_sync_ctx);
        assert_matches!(
            TcpSocketHandler::bind(
                &mut sync_ctx,
                &mut non_sync_ctx,
                unbound,
                *I::FAKE_CONFIG.remote_ip,
                None
            ),
            Err(BindError::NoLocalAddr)
        );

        sync_ctx.with_tcp_sockets(|sockets| {
            assert_matches!(sockets.inactive.get(unbound.into()), Some(_));
        });
    }

    // The test verifies that if client tries to connect to a closed port on
    // server, the connection is aborted and RST is received.
    #[ip_test]
    fn connect_reset<I: Ip + TcpTestIpExt>() {
        set_logger_for_test();
        let mut net = new_test_net::<I>();

        let client = net.with_context(LOCAL, |TcpCtx { sync_ctx, non_sync_ctx }| {
            let conn = TcpSocketHandler::create_socket(sync_ctx, non_sync_ctx);
            let conn = TcpSocketHandler::bind(
                sync_ctx,
                non_sync_ctx,
                conn,
                *I::FAKE_CONFIG.local_ip,
                Some(PORT_1),
            )
            .expect("failed to bind the client socket");
            TcpSocketHandler::connect_bound(
                sync_ctx,
                non_sync_ctx,
                conn,
                SocketAddr { ip: I::FAKE_CONFIG.remote_ip, port: PORT_1 },
                Default::default(),
            )
            .expect("failed to connect")
        });

        // Step one time for SYN packet to be delivered.
        let _: StepResult = net.step(handle_frame, handle_timer);
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

        net.run_until_idle(handle_frame, handle_timer);
        // Finally, the connection should be reset.
        let (conn, (), _): (_, _, &ConnAddr<_, _, _, _>) =
            client.get_from_socketmap(&net.sync_ctx(LOCAL).outer.sockets.socketmap);
        assert_matches!(
            conn,
            Connection {
                acceptor: None,
                state: State::Closed(Closed { reason: UserError::ConnectionReset }),
                ip_sock: _,
                defunct: false,
            }
        );
    }

    #[ip_test]
    fn retransmission<I: Ip + TcpTestIpExt>() {
        set_logger_for_test();
        run_with_many_seeds(|seed| {
            let (_net, _client, _accepted) =
                bind_listen_connect_accept_inner::<I>(I::UNSPECIFIED_ADDRESS, false, seed, 0.2);
        });
    }

    #[ip_test]
    fn bound_info<I: Ip + TcpTestIpExt>() {
        let TcpCtx { mut sync_ctx, mut non_sync_ctx } =
            TcpCtx::with_sync_ctx(TcpSyncCtx::<I>::new(
                I::FAKE_CONFIG.local_ip,
                I::FAKE_CONFIG.remote_ip,
                I::FAKE_CONFIG.subnet.prefix(),
            ));
        let unbound = TcpSocketHandler::create_socket(&mut sync_ctx, &mut non_sync_ctx);

        let (addr, port) = (I::FAKE_CONFIG.local_ip, PORT_1);
        let bound =
            TcpSocketHandler::bind(&mut sync_ctx, &mut non_sync_ctx, unbound, *addr, Some(port))
                .expect("bind should succeed");

        let info = TcpSocketHandler::get_bound_info(&sync_ctx, bound);
        assert_eq!(info, BoundInfo { addr: Some(addr), port, device: None });
    }

    #[ip_test]
    fn listener_info<I: Ip + TcpTestIpExt>() {
        let TcpCtx { mut sync_ctx, mut non_sync_ctx } =
            TcpCtx::with_sync_ctx(TcpSyncCtx::<I>::new(
                I::FAKE_CONFIG.local_ip,
                I::FAKE_CONFIG.remote_ip,
                I::FAKE_CONFIG.subnet.prefix(),
            ));
        let unbound = TcpSocketHandler::create_socket(&mut sync_ctx, &mut non_sync_ctx);

        let (addr, port) = (I::FAKE_CONFIG.local_ip, PORT_1);
        let bound =
            TcpSocketHandler::bind(&mut sync_ctx, &mut non_sync_ctx, unbound, *addr, Some(port))
                .expect("bind should succeed");
        let listener =
            TcpSocketHandler::listen(&mut sync_ctx, &mut non_sync_ctx, bound, nonzero!(25usize));

        let info = TcpSocketHandler::get_listener_info(&sync_ctx, listener);
        assert_eq!(info, BoundInfo { addr: Some(addr), port, device: None });
    }

    #[ip_test]
    fn connection_info<I: Ip + TcpTestIpExt>() {
        let TcpCtx { mut sync_ctx, mut non_sync_ctx } =
            TcpCtx::with_sync_ctx(TcpSyncCtx::<I>::new(
                I::FAKE_CONFIG.local_ip,
                I::FAKE_CONFIG.remote_ip,
                I::FAKE_CONFIG.subnet.prefix(),
            ));
        let local = SocketAddr { ip: I::FAKE_CONFIG.local_ip, port: PORT_1 };
        let remote = SocketAddr { ip: I::FAKE_CONFIG.remote_ip, port: PORT_2 };

        let unbound = TcpSocketHandler::create_socket(&mut sync_ctx, &mut non_sync_ctx);
        let bound = TcpSocketHandler::bind(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            *local.ip,
            Some(local.port),
        )
        .expect("bind should succeed");

        let connected = TcpSocketHandler::connect_bound(
            &mut sync_ctx,
            &mut non_sync_ctx,
            bound,
            remote,
            Default::default(),
        )
        .expect("connect should succeed");

        assert_eq!(
            TcpSocketHandler::get_connection_info(&sync_ctx, connected),
            ConnectionInfo { local_addr: local, remote_addr: remote, device: None }
        );
    }

    #[ip_test]
    fn connection_close<I: Ip + TcpTestIpExt>() {
        set_logger_for_test();
        let (mut net, local, remote) =
            bind_listen_connect_accept_inner::<I>(I::UNSPECIFIED_ADDRESS, false, 0, 0.0);
        net.with_context(REMOTE, |TcpCtx { sync_ctx, non_sync_ctx }| {
            TcpSocketHandler::close_conn(sync_ctx, non_sync_ctx, remote);
        });
        net.run_until_idle(handle_frame, handle_timer);
        net.with_context(REMOTE, |TcpCtx { sync_ctx, non_sync_ctx: _ }| {
            sync_ctx.with_tcp_sockets(|sockets| {
                let (conn, (), _addr) =
                    sockets.socketmap.conns().get_by_id(&remote.into()).expect("invalid conn ID");
                assert_matches!(conn.state, State::FinWait2(_));
            })
        });
        net.with_context(LOCAL, |TcpCtx { sync_ctx, non_sync_ctx }| {
            TcpSocketHandler::close_conn(sync_ctx, non_sync_ctx, local);
        });
        net.run_until_idle(handle_frame, handle_timer);

        for (name, id) in [(LOCAL, local), (REMOTE, remote)] {
            net.with_context(name, |TcpCtx { sync_ctx, non_sync_ctx: _ }| {
                sync_ctx.with_tcp_sockets(|sockets| {
                    assert_matches!(sockets.socketmap.conns().get_by_id(&id.into()), None);
                })
            });
        }
    }

    #[ip_test]
    fn connection_shutdown_then_close<I: Ip + TcpTestIpExt>() {
        set_logger_for_test();
        let (mut net, local, remote) =
            bind_listen_connect_accept_inner::<I>(I::UNSPECIFIED_ADDRESS, false, 0, 0.0);

        for (name, id) in [(LOCAL, local), (REMOTE, remote)] {
            net.with_context(name, |TcpCtx { sync_ctx, non_sync_ctx }| {
                assert_matches!(
                    TcpSocketHandler::shutdown_conn(sync_ctx, non_sync_ctx, id),
                    Ok(())
                );
                sync_ctx.with_tcp_sockets(|sockets| {
                    let (conn, (), _addr) = remote.get_from_socketmap(&sockets.socketmap);
                    assert_matches!(conn.state, State::FinWait1(_));
                });
                assert_matches!(
                    TcpSocketHandler::shutdown_conn(sync_ctx, non_sync_ctx, id),
                    Ok(())
                );
            });
        }
        net.run_until_idle(handle_frame, handle_timer);
        for (name, id) in [(LOCAL, local), (REMOTE, remote)] {
            net.with_context(name, |TcpCtx { sync_ctx, non_sync_ctx }| {
                sync_ctx.with_tcp_sockets(|sockets| {
                    let (conn, (), _addr) = remote.get_from_socketmap(&sockets.socketmap);
                    assert_matches!(conn.state, State::Closed(_));
                });
                TcpSocketHandler::close_conn(sync_ctx, non_sync_ctx, id);
                sync_ctx.with_tcp_sockets(|sockets| {
                    assert_matches!(sockets.socketmap.conns().get_by_id(&id.into()), None);
                })
            });
        }
    }

    #[ip_test]
    fn remove_unbound<I: Ip + TcpTestIpExt>() {
        let TcpCtx { mut sync_ctx, mut non_sync_ctx } =
            TcpCtx::with_sync_ctx(TcpSyncCtx::<I>::new(
                I::FAKE_CONFIG.local_ip,
                I::FAKE_CONFIG.remote_ip,
                I::FAKE_CONFIG.subnet.prefix(),
            ));
        let unbound = TcpSocketHandler::create_socket(&mut sync_ctx, &mut non_sync_ctx);
        TcpSocketHandler::remove_unbound(&mut sync_ctx, unbound);

        sync_ctx.with_tcp_sockets(|TcpSockets { socketmap: _, inactive, port_alloc: _ }| {
            assert_eq!(inactive.get(unbound.into()), None);
        })
    }

    #[ip_test]
    fn remove_bound<I: Ip + TcpTestIpExt>() {
        let TcpCtx { mut sync_ctx, mut non_sync_ctx } =
            TcpCtx::with_sync_ctx(TcpSyncCtx::<I>::new(
                I::FAKE_CONFIG.local_ip,
                I::FAKE_CONFIG.remote_ip,
                I::FAKE_CONFIG.subnet.prefix(),
            ));
        let unbound = TcpSocketHandler::create_socket(&mut sync_ctx, &mut non_sync_ctx);
        let bound = TcpSocketHandler::bind(
            &mut sync_ctx,
            &mut non_sync_ctx,
            unbound,
            *I::FAKE_CONFIG.local_ip,
            None,
        )
        .expect("bind should succeed");
        TcpSocketHandler::remove_bound(&mut sync_ctx, bound);

        sync_ctx.with_tcp_sockets(|TcpSockets { socketmap, inactive, port_alloc: _ }| {
            assert_eq!(inactive.get(unbound.into()), None);
            assert_eq!(socketmap.listeners().get_by_id(&bound.into()), None);
        })
    }

    #[ip_test]
    fn shutdown_listener<I: Ip + TcpTestIpExt>() {
        set_logger_for_test();
        let mut net = new_test_net::<I>();
        let local_listener = net.with_context(LOCAL, |TcpCtx { sync_ctx, non_sync_ctx }| {
            let unbound = TcpSocketHandler::create_socket(sync_ctx, non_sync_ctx);
            let bound = TcpSocketHandler::bind(
                sync_ctx,
                non_sync_ctx,
                unbound,
                *I::FAKE_CONFIG.local_ip,
                Some(PORT_1),
            )
            .expect("bind should succeed");
            TcpSocketHandler::listen(sync_ctx, non_sync_ctx, bound, NonZeroUsize::new(5).unwrap())
        });

        let remote_connection = net.with_context(REMOTE, |TcpCtx { sync_ctx, non_sync_ctx }| {
            let unbound = TcpSocketHandler::create_socket(sync_ctx, non_sync_ctx);
            TcpSocketHandler::connect_unbound(
                sync_ctx,
                non_sync_ctx,
                unbound,
                SocketAddr { ip: I::FAKE_CONFIG.local_ip, port: PORT_1 },
                Default::default(),
            )
            .expect("connect should succeed")
        });

        // After the following step, we should have one established connection
        // in the listener's accept queue, which ought to be aborted during
        // shutdown.
        net.run_until_idle(handle_frame, handle_timer);

        // Create a second half-open connection so that we have one entry in the
        // pending queue.
        net.with_context(REMOTE, |TcpCtx { sync_ctx, non_sync_ctx }| {
            let unbound = TcpSocketHandler::create_socket(sync_ctx, non_sync_ctx);
            let _: ConnectionId = TcpSocketHandler::connect_unbound(
                sync_ctx,
                non_sync_ctx,
                unbound,
                SocketAddr { ip: I::FAKE_CONFIG.local_ip, port: PORT_1 },
                Default::default(),
            )
            .expect("connect should succeed");
        });

        let _: StepResult = net.step(handle_frame, handle_timer);

        // We have a timer scheduled for the pending connection.
        net.with_context(LOCAL, |TcpCtx { sync_ctx: _, non_sync_ctx }| {
            assert_matches!(non_sync_ctx.timer_ctx().timers().len(), 1);
        });

        let local_bound = net.with_context(LOCAL, |TcpCtx { sync_ctx, non_sync_ctx }| {
            TcpSocketHandler::shutdown_listener(sync_ctx, non_sync_ctx, local_listener)
        });

        // The timer for the pending connection should be cancelled.
        net.with_context(LOCAL, |TcpCtx { sync_ctx: _, non_sync_ctx }| {
            assert_eq!(non_sync_ctx.timer_ctx().timers().len(), 0);
        });

        net.run_until_idle(handle_frame, handle_timer);

        // The remote socket should now be reset to Closed state.
        net.with_context(REMOTE, |TcpCtx { sync_ctx, non_sync_ctx: _ }| {
            sync_ctx.with_tcp_sockets(|sockets| {
                let (conn, (), _addr) = remote_connection.get_from_socketmap(&sockets.socketmap);
                assert_matches!(
                    conn.state,
                    State::Closed(Closed { reason: UserError::ConnectionReset })
                );
            });
        });

        net.with_context(LOCAL, |TcpCtx { sync_ctx, non_sync_ctx }| {
            let new_unbound = TcpSocketHandler::create_socket(sync_ctx, non_sync_ctx);
            assert_matches!(
                TcpSocketHandler::bind(
                    sync_ctx,
                    non_sync_ctx,
                    new_unbound,
                    *I::FAKE_CONFIG.local_ip,
                    Some(PORT_1),
                ),
                Err(BindError::Conflict)
            );
            // Bring the already-shutdown listener back to listener again.
            let _: ListenerId = TcpSocketHandler::listen(
                sync_ctx,
                non_sync_ctx,
                local_bound,
                NonZeroUsize::new(5).unwrap(),
            );
        });

        let new_remote_connection =
            net.with_context(REMOTE, |TcpCtx { sync_ctx, non_sync_ctx }| {
                let unbound = TcpSocketHandler::create_socket(sync_ctx, non_sync_ctx);
                TcpSocketHandler::connect_unbound(
                    sync_ctx,
                    non_sync_ctx,
                    unbound,
                    SocketAddr { ip: I::FAKE_CONFIG.local_ip, port: PORT_1 },
                    Default::default(),
                )
                .expect("connect should succeed")
            });

        net.run_until_idle(handle_frame, handle_timer);

        net.with_context(REMOTE, |TcpCtx { sync_ctx, non_sync_ctx: _ }| {
            sync_ctx.with_tcp_sockets(|sockets| {
                let (conn, (), _addr) =
                    new_remote_connection.get_from_socketmap(&sockets.socketmap);
                assert_matches!(conn.state, State::Established(_));
            });
        });
    }
}
