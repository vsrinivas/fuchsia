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

mod demux;
mod icmp;

use alloc::{collections::VecDeque, vec::Vec};
use core::{
    convert::Infallible as Never,
    marker::PhantomData,
    num::{NonZeroU16, NonZeroUsize},
};

use assert_matches::assert_matches;
use log::warn;
use net_types::{
    ip::{Ip, IpAddress},
    SpecifiedAddr,
};
use packet::Buf;
use packet_formats::ip::IpProto;

use crate::{
    context::InstantContext,
    data_structures::socketmap::{IterShadows as _, SocketMap},
    ip::{
        socket::{IpSock, IpSocket as _},
        BufferTransportIpContext, IpDeviceId,
    },
    socket::{
        posix::{
            ConnAddr, ConnIpAddr, ListenerAddr, ListenerIpAddr, PosixSharingOptions,
            PosixSocketMapSpec,
        },
        AddrVec, Bound, BoundSocketMap, InsertError,
    },
    transport::tcp::{
        buffer::{ReceiveBuffer, SendBuffer},
        seqnum::SeqNum,
        socket::demux::{tcp_serialize_segment, TcpBufferContext},
        state::{Closed, Initial, State},
    },
    IdMap, Instant, IpDeviceIdContext, IpExt, IpSockCreationError, IpSockSendError,
    TransportIpContext,
};

/// Socket address includes the ip address and the port number.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct SocketAddr<A: IpAddress> {
    ip: SpecifiedAddr<A>,
    port: NonZeroU16,
}

/// An implementation of [`IpTransportContext`] for TCP.
enum TcpIpTransportContext {}

/// Uninstantiatable type for implementing [`PosixSocketMapSpec`].
struct TcpPosixSocketSpec<Ip, Device, Instant, ReceiveBuffer, SendBuffer>(
    PhantomData<(Ip, Device, Instant, ReceiveBuffer, SendBuffer)>,
    Never,
);

impl<I: IpExt, D: IpDeviceId, II: Instant, R: ReceiveBuffer, S: SendBuffer> PosixSocketMapSpec
    for TcpPosixSocketSpec<I, D, II, R, S>
{
    type IpAddress = I::Addr;
    type DeviceId = D;
    type RemoteAddr = (SpecifiedAddr<I::Addr>, NonZeroU16);
    type LocalIdentifier = NonZeroU16;
    type ListenerId = MaybeListenerId;
    type ConnId = ConnectionId;

    type ListenerState = MaybeListener;
    type ConnState = Connection<I, D, II, R, S>;

    fn check_posix_sharing(
        new_sharing: PosixSharingOptions,
        addr: AddrVec<Self>,
        socketmap: &SocketMap<AddrVec<Self>, Bound<Self>>,
    ) -> Result<(), InsertError> {
        match new_sharing {
            PosixSharingOptions::Exclusive => {}
            PosixSharingOptions::ReusePort => {
                todo!("https://fxbug.dev/101596: Support port sharing for TCP")
            }
        }
        match &addr {
            AddrVec::Listen(_) => {
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
            // Connections don't conflict with existing listeners. If there
            // are connections with the same local and remote address, it
            // will be decided by the socket sharing options.
            AddrVec::Conn(_) => Ok(()),
        }
    }
}

/// Inactive sockets which are created but do not participate in demultiplexing
/// incoming segments.
#[derive(Debug, Clone, PartialEq)]
struct Inactive;

/// Holds all the TCP socket states.
struct TcpSockets<I: IpExt, D: IpDeviceId, II: Instant, R: ReceiveBuffer, S: SendBuffer> {
    inactive: IdMap<Inactive>,
    socketmap: BoundSocketMap<TcpPosixSocketSpec<I, D, II, R, S>>,
}

impl<I: IpExt, D: IpDeviceId, II: Instant, R: ReceiveBuffer, S: SendBuffer>
    TcpSockets<I, D, II, R, S>
{
    fn get_listener_by_id_mut(&mut self, id: ListenerId) -> Option<&mut Listener> {
        self.socketmap.get_listener_by_id_mut(MaybeListenerId::from(id)).map(
            |(maybe_listener, _sharing, _local_addr)| match maybe_listener {
                MaybeListener::Bound(_) => {
                    unreachable!("contract violated: ListenerId points to an inactive entry")
                }
                MaybeListener::Listener(l) => l,
            },
        )
    }
}

/// A link stored in each passively created connections that points back to the
/// parent listener.
///
/// The link is an [`Acceptor::Pending`] iff the acceptee is in the pending
/// state; The link is an [`Acceptor::Ready`] iff the acceptee is ready and has
/// an established connection.
#[cfg_attr(test, derive(Debug))]
enum Acceptor {
    Pending(ListenerId),
    Ready(ListenerId),
}

/// The Connection state.
///
/// Note: the `state` is not guaranteed to be [`State::Established`]. The
/// connection can be in any state as long as both the local and remote socket
/// addresses are specified.
#[cfg_attr(test, derive(Debug))]
struct Connection<I: IpExt, D: IpDeviceId, II: Instant, R: ReceiveBuffer, S: SendBuffer> {
    acceptor: Option<Acceptor>,
    state: State<II, R, S>,
    ip_sock: IpSock<I, D>,
}

/// The Listener state.
///
/// State for sockets that participate in the passive open. Contrary to
/// [`Connection`], only the local address is specified.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
struct Listener {
    backlog: NonZeroUsize,
    ready: VecDeque<ConnectionId>,
    pending: Vec<ConnectionId>,
    // If ip sockets can be half-specified so that only the local address
    // is needed, we can construct an ip socket here to be reused.
}

impl Listener {
    fn new(backlog: NonZeroUsize) -> Self {
        Self { backlog, ready: VecDeque::new(), pending: Vec::new() }
    }
}

/// Represents either a bound socket or a listener socket.
#[derive(Debug)]
enum MaybeListener {
    Bound(Inactive),
    Listener(Listener),
}

// TODO(https://fxbug.dev/38297): The following IDs are all `Clone + Copy`,
// which makes it possible for the client to keep them for longer than they are
// valid and cause panics. Find a way to make it harder to misuse.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct UnboundId(usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct BoundId(usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct ListenerId(usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct MaybeListenerId(usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct ConnectionId(usize);

fn create_socket<I, D, II, R, S, SC, C>(sync_ctx: &mut SC, _ctx: &mut C) -> UnboundId
where
    I: IpExt,
    D: IpDeviceId,
    II: Instant,
    R: ReceiveBuffer,
    S: SendBuffer,
    SC: AsMut<TcpSockets<I, D, II, R, S>>,
{
    UnboundId(sync_ctx.as_mut().inactive.push(Inactive))
}

#[derive(Debug)]
enum BindError {
    NoLocalAddr,
    Conflict(InsertError),
}

fn bind<I, II, R, S, SC, C>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    id: UnboundId,
    local_ip: I::Addr,
    port: Option<NonZeroU16>,
) -> Result<BoundId, BindError>
where
    I: IpExt,
    II: Instant,
    R: ReceiveBuffer,
    S: SendBuffer,
    SC: AsMut<TcpSockets<I, SC::DeviceId, II, R, S>> + TransportIpContext<I, C>,
{
    match port {
        None => todo!("https://fxbug.dev/101595: support port allocation"),
        Some(port) => bind_inner(sync_ctx, ctx, id, local_ip, port),
    }
}

fn bind_inner<I, II, R, S, SC, C>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    id: UnboundId,
    local_ip: I::Addr,
    port: NonZeroU16,
) -> Result<BoundId, BindError>
where
    I: IpExt,
    II: Instant,
    R: ReceiveBuffer,
    S: SendBuffer,
    SC: AsMut<TcpSockets<I, SC::DeviceId, II, R, S>> + TransportIpContext<I, C>,
{
    let idmap_key = id.into();
    let inactive = sync_ctx.as_mut().inactive.remove(idmap_key).expect("invalid unbound socket id");
    let local_ip = SpecifiedAddr::new(local_ip);
    if let Some(ip) = local_ip {
        if !sync_ctx.is_assigned_local_addr(*ip) {
            return Err(BindError::NoLocalAddr);
        }
    }
    sync_ctx
        .as_mut()
        .socketmap
        .try_insert_listener_with_sharing(
            ListenerAddr { ip: ListenerIpAddr { addr: local_ip, identifier: port }, device: None },
            MaybeListener::Bound(inactive.clone()),
            // TODO(https://fxbug.dev/101596): Support sharing for TCP sockets.
            PosixSharingOptions::Exclusive,
        )
        .map(|MaybeListenerId(x)| BoundId(x))
        .map_err(|err| {
            assert_eq!(sync_ctx.as_mut().inactive.insert(idmap_key, inactive), None);
            BindError::Conflict(err)
        })
}

fn listen<I, D, II, R, S, SC, C>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    id: BoundId,
    backlog: NonZeroUsize,
) -> ListenerId
where
    I: IpExt,
    D: IpDeviceId,
    II: Instant,
    R: ReceiveBuffer,
    S: SendBuffer,
    SC: AsMut<TcpSockets<I, D, II, R, S>>,
{
    let id = MaybeListenerId::from(id);
    let (listener, _, _): (_, PosixSharingOptions, &ListenerAddr<_>) =
        sync_ctx.as_mut().socketmap.get_listener_by_id_mut(id).expect("invalid listener id");
    match listener {
        MaybeListener::Bound(_) => {
            *listener = MaybeListener::Listener(Listener::new(backlog));
        }
        MaybeListener::Listener(_) => {
            unreachable!("invalid bound id that points to a listener entry")
        }
    }
    ListenerId(id.into())
}

#[derive(Debug)]
enum AcceptError {
    WouldBlock,
}

fn accept<I, D, II, R, S, SC, C>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    id: ListenerId,
) -> Result<(ConnectionId, SocketAddr<I::Addr>), AcceptError>
where
    I: IpExt,
    D: IpDeviceId,
    II: Instant,
    R: ReceiveBuffer,
    S: SendBuffer,
    SC: AsMut<TcpSockets<I, D, II, R, S>>,
{
    let listener = sync_ctx.as_mut().get_listener_by_id_mut(id).expect("invalid listener id");
    let conn_id = listener.ready.pop_front().ok_or(AcceptError::WouldBlock)?;
    let (conn, _, conn_addr): (_, PosixSharingOptions, _) = sync_ctx
        .as_mut()
        .socketmap
        .get_conn_by_id_mut(conn_id)
        .expect("failed to retrieve the connection state");
    conn.acceptor = None;
    let (remote_ip, remote_port) = conn_addr.ip.remote;
    Ok((conn_id, SocketAddr { ip: remote_ip, port: remote_port }))
}

#[derive(Debug)]
enum ConnectError {
    IpSockCreationError(IpSockCreationError),
    IpSocketSendError(IpSockSendError),
}

fn connect<I, D, II, R, S, SC, C>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    id: BoundId,
    remote: SocketAddr<I::Addr>,
    isn: SeqNum,
) -> Result<ConnectionId, ConnectError>
where
    I: IpExt,
    D: IpDeviceId,
    II: Instant,
    R: ReceiveBuffer,
    S: SendBuffer,
    SC: AsMut<TcpSockets<I, D, II, R, S>>
        + BufferTransportIpContext<I, C, Buf<Vec<u8>>, DeviceId = D>
        + InstantContext<Instant = II>,
{
    let bound_id = MaybeListenerId::from(id);
    let (bound, bound_addr) =
        sync_ctx.as_mut().socketmap.get_listener_by_id(&bound_id).expect("invalid socket id");
    assert_matches!(bound, (MaybeListener::Bound(_), PosixSharingOptions::Exclusive));
    let ListenerAddr { ip: ListenerIpAddr { addr: local_ip, identifier: local_port }, device } =
        *bound_addr;
    let ip_sock = sync_ctx
        .new_ip_socket(ctx, device, local_ip, remote.ip, IpProto::Tcp.into(), None)
        .map_err(ConnectError::IpSockCreationError)?;
    let conn_addr = ConnAddr {
        ip: ConnIpAddr {
            local_ip: ip_sock.local_ip().clone(),
            local_identifier: local_port,
            remote: (remote.ip, remote.port),
        },
        // TODO(https://fxbug.dev/102103): Support SO_BINDTODEVICE.
        device: None,
    };
    let now = sync_ctx.now();
    let (syn_sent, syn) = Closed::<Initial>::connect(isn, now);
    let conn_id = sync_ctx
        .as_mut()
        .socketmap
        .try_insert_conn_with_sharing(
            conn_addr.clone(),
            Connection {
                acceptor: None,
                state: State::SynSent(syn_sent),
                ip_sock: ip_sock.clone(),
            },
            // TODO(https://fxbug.dev/101596): Support sharing for TCP sockets.
            PosixSharingOptions::Exclusive,
        )
        .expect("failed to insert connection");
    sync_ctx
        .send_ip_packet(ctx, &ip_sock, tcp_serialize_segment(syn, conn_addr.ip), None)
        .map_err(|(body, err)| {
            warn!("tcp: failed to send ip packet {:?}: {:?}", body, err);
            ConnectError::IpSocketSendError(err)
        })?;
    let _: Option<_> = sync_ctx.as_mut().socketmap.remove_listener_by_id(bound_id);
    Ok(conn_id)
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
    use core::fmt::Debug;
    use net_types::ip::{AddrSubnet, Ipv4, Ipv6, Ipv6SourceAddr};
    use packet::ParseBuffer as _;
    use packet_formats::tcp::{TcpParseArgs, TcpSegment};
    use specialize_ip_macro::ip_test;

    use crate::{
        context::testutil::{
            DummyCtx, DummyInstant, DummyNetwork, DummyNetworkLinks, DummySyncCtx, InstantAndData,
            PendingFrameData, StepResult,
        },
        ip::{
            device::state::{
                AddrConfig, AddressState, IpDeviceState, IpDeviceStateIpExt, Ipv6AddressEntry,
            },
            socket::{testutil::DummyIpSocketCtx, BufferIpSocketHandler, IpSocketHandler},
            BufferIpTransportContext as _, DummyDeviceId, SendIpPacketMeta,
        },
        testutil::set_logger_for_test,
        transport::tcp::{buffer::RingBuffer, UserError},
    };

    use super::*;

    trait TcpTestIpExt: IpExt + crate::testutil::TestIpExt + IpDeviceStateIpExt<DummyInstant> {
        fn recv_src_addr(addr: Self::Addr) -> Self::RecvSrcAddr;

        fn new_device_state(addr: Self::Addr, prefix: u8) -> IpDeviceState<DummyInstant, Self>;
    }

    struct TcpState<I: TcpTestIpExt> {
        sockets: TcpSockets<I, DummyDeviceId, DummyInstant, RingBuffer, RingBuffer>,
        ip_socket_ctx: DummyIpSocketCtx<I, DummyDeviceId>,
    }

    type TcpCtx<I> = DummyCtx<
        TcpState<I>,
        (),
        SendIpPacketMeta<I, DummyDeviceId, SpecifiedAddr<<I as Ip>::Addr>>,
        (),
        DummyDeviceId,
    >;

    type TcpSyncCtx<I> = DummySyncCtx<
        TcpState<I>,
        (),
        SendIpPacketMeta<I, DummyDeviceId, SpecifiedAddr<<I as Ip>::Addr>>,
        (),
        DummyDeviceId,
    >;

    impl<I: TcpTestIpExt> TcpBufferContext for TcpSyncCtx<I> {
        type ReceiveBuffer = RingBuffer;
        type SendBuffer = RingBuffer;
    }

    impl<I: TcpTestIpExt> TransportIpContext<I, ()> for TcpSyncCtx<I>
    where
        TcpSyncCtx<I>: IpSocketHandler<I, ()>,
    {
        fn is_assigned_local_addr(&self, _addr: <I as Ip>::Addr) -> bool {
            true
        }
    }

    impl<I: TcpTestIpExt> AsMut<TcpSockets<I, DummyDeviceId, DummyInstant, RingBuffer, RingBuffer>>
        for TcpSyncCtx<I>
    {
        fn as_mut(
            &mut self,
        ) -> &mut TcpSockets<I, DummyDeviceId, DummyInstant, RingBuffer, RingBuffer> {
            &mut self.get_mut().sockets
        }
    }

    impl<I: TcpTestIpExt> AsRef<TcpSockets<I, DummyDeviceId, DummyInstant, RingBuffer, RingBuffer>>
        for TcpSyncCtx<I>
    {
        fn as_ref(&self) -> &TcpSockets<I, DummyDeviceId, DummyInstant, RingBuffer, RingBuffer> {
            &self.get_ref().sockets
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
                sockets: TcpSockets {
                    inactive: IdMap::new(),
                    socketmap: BoundSocketMap::default(),
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
        DummyCtx { sync_ctx, non_sync_ctx }: &mut TcpCtx<I>,
        meta: SendIpPacketMeta<I, DummyDeviceId, SpecifiedAddr<I::Addr>>,
        buffer: Buf<Vec<u8>>,
    ) where
        TcpSyncCtx<I>: BufferIpSocketHandler<I, (), Buf<Vec<u8>>>
            + InstantContext<Instant = DummyInstant>
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

    fn panic_if_any_timer<Ctx, Timer: Debug>(_sc: &mut Ctx, _c: &mut (), timer: Timer) {
        panic!("unexpected timer fired: {:?}", timer)
    }

    // The following test sets up two connected testing context - one as the
    // server and the other as the client. Tests if a connection can be
    // established using `bind`, `listen`, `connect` and `accept`.
    #[ip_test]
    fn bind_listen_connect_accept<I: Ip + TcpTestIpExt>()
    where
        TcpSyncCtx<I>: BufferIpSocketHandler<I, (), Buf<Vec<u8>>>
            + InstantContext<Instant = DummyInstant>
            + IpDeviceIdContext<I, DeviceId = DummyDeviceId>,
    {
        set_logger_for_test();
        let mut net = new_test_net::<I>();

        let server = create_socket(net.sync_ctx(REMOTE), &mut ());
        let client = create_socket(net.sync_ctx(LOCAL), &mut ());
        let server =
            bind(net.sync_ctx(REMOTE), &mut (), server, *I::DUMMY_CONFIG.remote_ip, Some(PORT_1))
                .expect("failed to bind the servekr socket");
        let client =
            bind(net.sync_ctx(LOCAL), &mut (), client, *I::DUMMY_CONFIG.local_ip, Some(PORT_1))
                .expect("failed to bind the client socket");
        let backlog = NonZeroUsize::new(1).unwrap();
        let server = listen(net.sync_ctx(REMOTE), &mut (), server, backlog);
        let client = connect(
            net.sync_ctx(LOCAL),
            &mut (),
            client,
            SocketAddr { ip: I::DUMMY_CONFIG.remote_ip, port: PORT_1 },
            SeqNum::new(0),
        )
        .expect("failed to connect");

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
        assert_matches!(
            accept(net.sync_ctx(REMOTE), &mut (), server),
            Err(AcceptError::WouldBlock)
        );

        // Step the test network until the handshake is done.
        net.run_until_idle(handle_frame, panic_if_any_timer);
        let (accepted, addr) =
            accept(net.sync_ctx(REMOTE), &mut (), server).expect("failed to accept");
        assert_eq!(addr, SocketAddr { ip: I::DUMMY_CONFIG.local_ip, port: PORT_1 },);

        let mut assert_connected = |name: &'static str, conn_id: ConnectionId| {
            let (state, _): &(_, ConnAddr<_>) = net
                .sync_ctx(name)
                .get_ref()
                .sockets
                .socketmap
                .get_conn_by_id(&conn_id)
                .expect("failed to retrieve the client socket");
            assert_matches!(
                state,
                (
                    Connection { acceptor: None, state: State::Established(_), ip_sock: _ },
                    PosixSharingOptions::Exclusive
                )
            );
        };

        assert_connected(LOCAL, client);
        assert_connected(REMOTE, accepted);

        // Check the listener is in correct state.
        assert_eq!(
            net.sync_ctx(REMOTE).get_mut().sockets.get_listener_by_id_mut(server),
            Some(&mut Listener::new(backlog)),
        );
    }

    // TODO(https://fxbug.dev/102105): The following tests are similar in that
    // they should be able to be unified with the `test_case` macro. Rewrite the
    // tests when `test_case` works well with `ip_test`.
    #[ip_test]
    fn bind_conflict_same_addr<I: Ip + TcpTestIpExt>()
    where
        TcpSyncCtx<I>: BufferIpSocketHandler<I, (), Buf<Vec<u8>>>
            + InstantContext<Instant = DummyInstant>
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
            bind(&mut sync_ctx, &mut non_sync_ctx, s2, *I::DUMMY_CONFIG.local_ip, Some(PORT_1)),
            Err(BindError::Conflict(_))
        );
        let _b2 =
            bind(&mut sync_ctx, &mut non_sync_ctx, s2, *I::DUMMY_CONFIG.local_ip, Some(PORT_2))
                .expect("able to rebind to a free address");
    }

    #[ip_test]
    fn bind_conflict_any_addr<I: Ip + TcpTestIpExt>()
    where
        TcpSyncCtx<I>: BufferIpSocketHandler<I, (), Buf<Vec<u8>>>
            + InstantContext<Instant = DummyInstant>
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
            bind(&mut sync_ctx, &mut non_sync_ctx, s2, I::UNSPECIFIED_ADDRESS, Some(PORT_1)),
            Err(BindError::Conflict(_))
        );
        let _b2 = bind(&mut sync_ctx, &mut non_sync_ctx, s2, I::UNSPECIFIED_ADDRESS, Some(PORT_2))
            .expect("able to rebind to a free address");
    }

    // The test verifies that if client tries to connect to a closed port on
    // server, the connection is aborted and RST is received.
    #[ip_test]
    fn connect_reset<I: Ip + TcpTestIpExt>()
    where
        TcpSyncCtx<I>: BufferIpSocketHandler<I, (), Buf<Vec<u8>>>
            + InstantContext<Instant = DummyInstant>
            + IpDeviceIdContext<I, DeviceId = DummyDeviceId>,
    {
        set_logger_for_test();
        let mut net = new_test_net::<I>();

        let client = create_socket(net.sync_ctx(LOCAL), &mut ());
        let client =
            bind(net.sync_ctx(LOCAL), &mut (), client, *I::DUMMY_CONFIG.local_ip, Some(PORT_1))
                .expect("failed to bind the client socket");
        let client = connect(
            net.sync_ctx(LOCAL),
            &mut (),
            client,
            SocketAddr { ip: I::DUMMY_CONFIG.remote_ip, port: NonZeroU16::new(42).unwrap() },
            SeqNum::new(0),
        )
        .expect("failed to connect");

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
        let ((state, _), _): &((_, PosixSharingOptions), ConnAddr<_>) = net
            .sync_ctx(LOCAL)
            .get_ref()
            .sockets
            .socketmap
            .get_conn_by_id(&client)
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
