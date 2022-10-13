// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The transport layer.
//!
//! # Listeners and connections
//!
//! Some transport layer protocols (notably TCP and UDP) follow a common pattern
//! with respect to registering listeners and connections. There are some
//! subtleties here that are worth pointing out.
//!
//! ## Connections
//!
//! A connection has simpler semantics than a listener. It is bound to a single
//! local address and port and a single remote address and port. By virtue of
//! being bound to a local address, it is also bound to a local interface. This
//! means that, regardless of the entries in the forwarding table, all traffic
//! on that connection will always egress over the same interface. [^1] This
//! also means that, if the interface's address changes, any connections bound
//! to it are severed.
//!
//! ## Listeners
//!
//! A listener, on the other hand, can be bound to any number of local addresses
//! (although it is still always bound to a particular port). From the
//! perspective of this crate, there are two ways of registering a listener:
//! - By specifying one or more local addresses, the listener will be bound to
//!   each of those local addresses.
//! - By specifying zero local addresses, the listener will be bound to all
//!   addresses. These are referred to in our documentation as "wildcard
//!   listeners".
//!
//! The algorithm for figuring out what listener to deliver a packet to is as
//! follows: If there is any listener bound to the specific local address and
//! port addressed in the packet, deliver the packet to that listener.
//! Otherwise, if there is a wildcard listener bound the port addressed in the
//! packet, deliver the packet to that listener. This implies that if a listener
//! is removed which was bound to a particular local address, it can "uncover" a
//! wildcard listener bound to the same port, allowing traffic which would
//! previously have been delivered to the normal listener to now be delivered to
//! the wildcard listener.
//!
//! If desired, clients of this crate can implement a different mechanism for
//! registering listeners on all local addresses - enumerate every local
//! address, and then specify all of the local addresses when registering the
//! listener. This approach will not support shadowing, as a different listener
//! binding to the same port will explicitly conflict with the existing
//! listener, and will thus be rejected. In other words, from the perspective of
//! this crate's API, such listeners will appear like normal listeners that just
//! happen to bind all of the addresses, rather than appearing like wildcard
//! listeners.
//!
//! [^1]: It is an open design question as to whether incoming traffic on the
//!       connection will be accepted from a different interface. This is part
//!       of the "weak host model" vs "strong host model" discussion.

mod integration;
// TODO(https://fxbug.dev/95688): Integrate TCP with the rest of netstack3 core.
// Note: we can't use `todo_unused` here because of the following issue:
// https://github.com/rust-lang/rust/issues/54727
#[allow(unused)]
pub mod tcp;
pub mod udp;

use net_types::ip::{Ipv4, Ipv6};

use crate::{
    device::DeviceId,
    transport::tcp::socket::TcpNonSyncContext,
    transport::{
        tcp::{
            socket::{isn::IsnGenerator, TcpSockets, TcpSyncContext},
            TcpState,
        },
        udp::{UdpState, UdpStateBuilder},
    },
    NonSyncContext, RngContext, SyncCtx,
};

/// A builder for transport layer state.
#[derive(Default, Clone)]
pub struct TransportStateBuilder {
    udp: UdpStateBuilder,
}

impl TransportStateBuilder {
    /// Get the builder for the UDP state.
    pub fn udp_builder(&mut self) -> &mut UdpStateBuilder {
        &mut self.udp
    }

    pub(crate) fn build_with_ctx<C: TcpNonSyncContext + RngContext>(
        self,
        ctx: &mut C,
    ) -> TransportLayerState<C> {
        TransportLayerState {
            udpv4: self.udp.clone().build(),
            udpv6: self.udp.build(),
            tcpv4: TcpState::new(ctx.now(), ctx.rng_mut()),
            tcpv6: TcpState::new(ctx.now(), ctx.rng_mut()),
        }
    }
}

/// The state associated with the transport layer.
pub(crate) struct TransportLayerState<C: TcpNonSyncContext> {
    udpv4: UdpState<Ipv4, DeviceId<C::Instant>>,
    udpv6: UdpState<Ipv6, DeviceId<C::Instant>>,
    tcpv4: TcpState<Ipv4, DeviceId<C::Instant>, C>,
    tcpv6: TcpState<Ipv6, DeviceId<C::Instant>, C>,
}

impl<C: NonSyncContext> TcpSyncContext<Ipv4, C> for &'_ SyncCtx<C> {
    type IpTransportCtx = Self;

    fn with_ip_transport_ctx_isn_generator_and_tcp_sockets_mut<
        O,
        F: FnOnce(&mut Self, &IsnGenerator<C::Instant>, &mut TcpSockets<Ipv4, Self::DeviceId, C>) -> O,
    >(
        &mut self,
        cb: F,
    ) -> O {
        let mut s = *self;
        let TcpState { isn_generator, sockets } = &s.state.transport.tcpv4;
        cb(&mut s, isn_generator, &mut sockets.lock())
    }

    fn with_tcp_sockets<O, F: FnOnce(&TcpSockets<Ipv4, Self::DeviceId, C>) -> O>(
        &self,
        cb: F,
    ) -> O {
        let TcpState { sockets, isn_generator: _ } = &self.state.transport.tcpv4;
        cb(&sockets.lock())
    }
}

impl<C: NonSyncContext> TcpSyncContext<Ipv6, C> for &'_ SyncCtx<C> {
    type IpTransportCtx = Self;

    fn with_ip_transport_ctx_isn_generator_and_tcp_sockets_mut<
        O,
        F: FnOnce(&mut Self, &IsnGenerator<C::Instant>, &mut TcpSockets<Ipv6, Self::DeviceId, C>) -> O,
    >(
        &mut self,
        cb: F,
    ) -> O {
        let mut s = *self;
        let TcpState { isn_generator, sockets } = &s.state.transport.tcpv6;
        cb(&mut s, isn_generator, &mut sockets.lock())
    }

    fn with_tcp_sockets<O, F: FnOnce(&TcpSockets<Ipv6, Self::DeviceId, C>) -> O>(
        &self,
        cb: F,
    ) -> O {
        let TcpState { sockets, isn_generator: _ } = &self.state.transport.tcpv6;
        cb(&sockets.lock())
    }
}

/// The identifier for timer events in the transport layer.
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub(crate) enum TransportLayerTimerId {
    Tcp(tcp::socket::TimerId),
}

/// Handle a timer event firing in the transport layer.
pub(crate) fn handle_timer<NonSyncCtx: NonSyncContext>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    id: TransportLayerTimerId,
) {
    match id {
        TransportLayerTimerId::Tcp(id) => tcp::socket::handle_timer(&mut sync_ctx, ctx, id),
    }
}

impl From<tcp::socket::TimerId> for TransportLayerTimerId {
    fn from(id: tcp::socket::TimerId) -> Self {
        TransportLayerTimerId::Tcp(id)
    }
}

impl_timer_context!(
    TransportLayerTimerId,
    tcp::socket::TimerId,
    TransportLayerTimerId::Tcp(id),
    id
);
