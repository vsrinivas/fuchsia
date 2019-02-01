// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Protocol, versions 4 and 6.

mod forwarding;
mod icmp;
#[cfg(test)]
mod testdata;
mod types;

pub use self::types::*;

use log::{debug, trace};
use std::fmt::Debug;
use std::mem;

use packet::{BufferMut, BufferSerializer, ParsablePacket, ParseBufferMut, Serializer};
use zerocopy::{ByteSlice, ByteSliceMut};

use crate::device::DeviceId;
use crate::ip::forwarding::{Destination, ForwardingTable};
use crate::wire::ipv4::{Ipv4Packet, Ipv4PacketBuilder};
use crate::wire::ipv6::{Ipv6Packet, Ipv6PacketBuilder};
use crate::{Context, EventDispatcher};

// default IPv4 TTL or IPv6 hops
const DEFAULT_TTL: u8 = 64;

/// The state associated with the IP layer.
#[derive(Default)]
pub struct IpLayerState {
    v4: IpLayerStateInner<Ipv4>,
    v6: IpLayerStateInner<Ipv6>,
}

#[derive(Default)]
struct IpLayerStateInner<I: Ip> {
    forward: bool,
    table: ForwardingTable<I>,
}

fn dispatch_receive_ip_packet<D: EventDispatcher, I: IpAddr, B: BufferMut>(
    ctx: &mut Context<D>,
    proto: IpProto,
    src_ip: I,
    dst_ip: I,
    mut buffer: B,
) -> bool {
    increment_counter!(ctx, "dispatch_receive_ip_packet");
    match proto {
        IpProto::Icmp | IpProto::Icmpv6 => icmp::receive_icmp_packet(ctx, src_ip, dst_ip, buffer),
        IpProto::Tcp | IpProto::Udp => {
            crate::transport::receive_ip_packet(ctx, src_ip, dst_ip, proto, buffer)
        }
    }
}

/// Receive an IP packet from a device.
pub fn receive_ip_packet<D: EventDispatcher, B: BufferMut, I: Ip>(
    ctx: &mut Context<D>,
    device: DeviceId,
    mut buffer: B,
) {
    trace!("receive_ip_packet({})", device);
    let mut packet = if let Ok(packet) = buffer.parse_mut::<<I as IpExt<_>>::Packet>() {
        packet
    } else {
        // TODO(joshlf): Do something with ICMP here?
        return;
    };
    trace!("receive_ip_packet: parsed packet: {:?}", packet);

    if I::LOOPBACK_SUBNET.contains(packet.dst_ip()) {
        // A packet from outside this host was sent with the destination IP of
        // the loopback address, which is illegal. Loopback traffic is handled
        // explicitly in send_ip_packet. TODO(joshlf): Do something with ICMP
        // here?
        debug!("got packet from remote host for loopback address {}", packet.dst_ip());
    } else if deliver(ctx, device, packet.dst_ip()) {
        trace!("receive_ip_packet: delivering locally");
        // TODO(joshlf):
        // - Do something with ICMP if we don't have a handler for that protocol?
        // - Check for already-expired TTL?
        let handled = if let Ok(proto) = packet.proto() {
            let src_ip = packet.src_ip();
            let dst_ip = packet.dst_ip();
            // drop packet so we can re-use the underlying buffer
            mem::drop(packet);
            dispatch_receive_ip_packet(ctx, proto, src_ip, dst_ip, buffer)
        } else {
            // TODO(joshlf): Log unrecognized protocol number
            false
        };
    } else if let Some(dest) = forward(ctx, packet.dst_ip()) {
        let ttl = packet.ttl();
        if ttl > 1 {
            trace!("receive_ip_packet: forwarding");
            packet.set_ttl(ttl - 1);
            let meta = packet.parse_metadata();
            // drop packet so we can re-use the underlying buffer
            mem::drop(packet);
            // Undo the effects of parsing so that the body of the buffer
            // contains the entire IP packet again (not just the body).
            buffer.undo_parse(meta);
            crate::device::send_ip_frame(
                ctx,
                dest.device,
                dest.next_hop,
                BufferSerializer::new_vec(buffer),
            );
            return;
        } else {
            // TTL is 0 or would become 0 after decrement; see "TTL" section,
            // https://tools.ietf.org/html/rfc791#page-14
            // TODO(joshlf): Do something with ICMP here?
            debug!("received IP packet dropped due to expired TTL");
        }
    } else {
        // TODO(joshlf): Do something with ICMP here?
        debug!("received IP packet with no known route to destination {}", packet.dst_ip());
    }
}

/// Get the local address of the interface that will be used to route to a
/// remote address.
///
/// `local_address_for_remote` looks up the route to `remote`. If one is found,
/// it returns the IP address of the interface specified by the route, or `None`
/// if the interface has no IP address.
pub fn local_address_for_remote<D: EventDispatcher, A: IpAddr>(
    ctx: &mut Context<D>,
    remote: A,
) -> Option<A> {
    let route = lookup_route(&ctx.state().ip, remote)?;
    crate::device::get_ip_addr(ctx, route.device).map(|(addr, _)| addr)
}

// Should we deliver this packet locally?
// deliver returns true if:
// - dst_ip is equal to the address set on the device
// - dst_ip is equal to the broadcast address of the subnet set on the device
// - dst_ip is equal to the global broadcast address
fn deliver<D: EventDispatcher, A: IpAddr>(
    ctx: &mut Context<D>,
    device: DeviceId,
    dst_ip: A,
) -> bool {
    // TODO(joshlf):
    // - This implements a strict host model (in which we only accept packets
    //   which are addressed to the device over which they were received). This
    //   is the easiest to implement for the time being, but we should actually
    //   put real thought into what our host model should be (NET-1011).
    specialize_ip_addr!(
        fn deliver(dst_ip: Self, addr_subnet: Option<(Self, Subnet<Self>)>) -> bool {
            Ipv4Addr => {
                addr_subnet
                    .map(|(addr, subnet)| dst_ip == addr || dst_ip == subnet.broadcast())
                    .unwrap_or(dst_ip == Ipv4::BROADCAST_ADDRESS)
            }
            Ipv6Addr => { log_unimplemented!(false, "ip::deliver: Ipv6 not implemeneted") }
        }
    );
    A::deliver(dst_ip, crate::device::get_ip_addr::<D, A>(ctx, device))
}

// Should we forward this packet, and if so, to whom?
fn forward<D: EventDispatcher, A: IpAddr>(
    ctx: &mut Context<D>,
    dst_ip: A,
) -> Option<Destination<A::Version>> {
    specialize_ip_addr!(
        fn forwarding_enabled(state: &IpLayerState) -> bool {
            Ipv4Addr => { state.v4.forward }
            Ipv6Addr => { state.v6.forward }
        }
    );
    let ip_state = &ctx.state().ip;
    if A::forwarding_enabled(ip_state) {
        lookup_route(ip_state, dst_ip)
    } else {
        None
    }
}

// Look up the route to a host.
fn lookup_route<A: IpAddr>(state: &IpLayerState, dst_ip: A) -> Option<Destination<A::Version>> {
    specialize_ip_addr!(
        fn get_table(state: &IpLayerState) -> &ForwardingTable<Self::Version> {
            Ipv4Addr => { &state.v4.table }
            Ipv6Addr => { &state.v6.table }
        }
    );
    A::get_table(state).lookup(dst_ip)
}

/// Add a route to the forwarding table.
pub fn add_device_route<D: EventDispatcher, A: IpAddr>(
    ctx: &mut Context<D>,
    subnet: Subnet<A>,
    device: DeviceId,
) {
    specialize_ip_addr!(
        fn generic_add_route(state: &mut IpLayerState, subnet: Subnet<Self>, device: DeviceId) {
            Ipv4Addr => { state.v4.table.add_device_route(subnet, device) }
            Ipv6Addr => { state.v6.table.add_device_route(subnet, device) }
        }
    );
    A::generic_add_route(&mut ctx.state().ip, subnet, device)
}

/// Is this one of our local addresses?
///
/// `is_local_addr` returns whether `addr` is the address associated with one of
/// our local interfaces.
pub fn is_local_addr<D: EventDispatcher, A: IpAddr>(ctx: &mut Context<D>, addr: A) -> bool {
    log_unimplemented!(false, "ip::is_local_addr: not implemented")
}

/// Send an IP packet to a remote host.
///
/// `send_ip_packet` accepts a destination IP address, a protocol, and a
/// callback. It computes the routing information, and invokes the callback with
/// the computed destination address. The callback returns a
/// `SerializationRequest`, which is serialized in a new IP packet and sent.
pub fn send_ip_packet<D: EventDispatcher, A, S, F>(
    ctx: &mut Context<D>,
    dst_ip: A,
    proto: IpProto,
    get_body: F,
) where
    A: IpAddr,
    S: Serializer,
    F: FnOnce(A) -> S,
{
    trace!("send_ip_packet({}, {})", dst_ip, proto);
    increment_counter!(ctx, "send_ip_packet");
    if A::Version::LOOPBACK_SUBNET.contains(dst_ip) {
        increment_counter!(ctx, "send_ip_packet::loopback");
        // TODO(joshlf): Currently, we serialize using the normal Serializer
        // functionality. I wonder if, in the case of delivering to loopback, we
        // can do something more efficient?
        let mut buffer = get_body(A::Version::LOOPBACK_ADDRESS).serialize_outer();
        // TODO(joshlf): Respond with some kind of error if we don't have a
        // handler for that protocol? Maybe simulate what would have happened
        // (w.r.t ICMP) if this were a remote host?
        dispatch_receive_ip_packet(
            ctx,
            proto,
            A::Version::LOOPBACK_ADDRESS,
            dst_ip,
            buffer.as_buf_mut(),
        );
    } else if let Some(dest) = lookup_route(&ctx.state().ip, dst_ip) {
        let (src_ip, _) = crate::device::get_ip_addr(ctx, dest.device)
            .expect("IP device route set for device without IP address");
        send_ip_packet_from_device(
            ctx,
            dest.device,
            src_ip,
            dst_ip,
            dest.next_hop,
            proto,
            get_body(src_ip),
        );
    } else {
        debug!("No route to host");
        // TODO(joshlf): No route to host
    }
}

/// Send an IP packet to a remote host from a specific source address.
///
/// `send_ip_packet_from` accepts a source and destination IP address and a
/// `SerializationRequest`. It computes the routing information and serializes
/// the request in a new IP packet and sends it.
///
/// `send_ip_packet_from` computes a route to the destination with the
/// restriction that the packet must originate from the source address, and must
/// eagress over the interface associated with that source address. If this
/// restriction cannot be met, a "no route to host" error is returned.
pub fn send_ip_packet_from<D: EventDispatcher, A, S>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    body: S,
) where
    A: IpAddr,
    S: Serializer,
{
    // TODO(joshlf): Figure out how to compute a route with the restrictions
    // mentioned in the doc comment.
    log_unimplemented!((), "ip::send_ip_packet_from: not implemented");
}

/// Send an IP packet to a remote host over a specific device.
///
/// `send_ip_packet_from_device` accepts a device, a source and destination IP
/// address, a next hop IP address, and a `SerializationRequest`. It computes
/// the routing information and serializes the request in a new IP packet and
/// sends it.
///
/// # Panics
///
/// Since `send_ip_packet_from_device` specifies a physical device, it cannot
/// send to or from a loopback IP address. If either `src_ip` or `dst_ip` are in
/// the loopback subnet, `send_ip_packet_from_device` will panic.
pub fn send_ip_packet_from_device<D: EventDispatcher, A, S>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: A,
    dst_ip: A,
    next_hop: A,
    proto: IpProto,
    body: S,
) where
    A: IpAddr,
    S: Serializer,
{
    assert!(!A::Version::LOOPBACK_SUBNET.contains(src_ip));
    assert!(!A::Version::LOOPBACK_SUBNET.contains(dst_ip));

    specialize_ip_addr!(
        fn serialize<D, S>(
            ctx: &mut Context<D>, device: DeviceId, src_ip: Self, dst_ip: Self, next_hop: Self, ttl: u8, proto: IpProto, body: S
        )
        where
            D: EventDispatcher,
            S: Serializer,
        {
            Ipv4Addr => {
                let body = body.encapsulate(Ipv4PacketBuilder::new(src_ip, dst_ip, ttl, proto));
                crate::device::send_ip_frame(ctx, device, next_hop, body);
            }
            Ipv6Addr => {
                let body = body.encapsulate(Ipv6PacketBuilder::new(src_ip, dst_ip, ttl, proto));
                crate::device::send_ip_frame(ctx, device, next_hop, body);
            }
        }
    );
    A::serialize(ctx, device, src_ip, dst_ip, next_hop, DEFAULT_TTL, proto, body)
}

// An `Ip` extension trait for internal use.
//
// This trait adds extra associated types that are useful for our implementation
// here, but which consumers outside of the ip module do not need to see.
trait IpExt<B: ByteSlice>: Ip {
    type Packet: IpPacket<B, Self>;
}

impl<B: ByteSlice, I: Ip> IpExt<B> for I {
    default type Packet = !;
}

impl<B: ByteSlice> IpExt<B> for Ipv4 {
    type Packet = Ipv4Packet<B>;
}

impl<B: ByteSlice> IpExt<B> for Ipv6 {
    type Packet = Ipv6Packet<B>;
}

// `Ipv4Packet` or `Ipv6Packet`
trait IpPacket<B: ByteSlice, I: Ip>: Sized + Debug + ParsablePacket<B, ()> {
    fn src_ip(&self) -> I::Addr;
    fn dst_ip(&self) -> I::Addr;
    fn proto(&self) -> Result<IpProto, u8>;
    fn ttl(&self) -> u8;
    fn set_ttl(&mut self, ttl: u8)
    where
        B: ByteSliceMut;
}

impl<B: ByteSlice> IpPacket<B, Ipv4> for Ipv4Packet<B> {
    fn src_ip(&self) -> Ipv4Addr {
        Ipv4Packet::src_ip(self)
    }
    fn dst_ip(&self) -> Ipv4Addr {
        Ipv4Packet::dst_ip(self)
    }
    fn proto(&self) -> Result<IpProto, u8> {
        Ipv4Packet::proto(self)
    }
    fn ttl(&self) -> u8 {
        Ipv4Packet::ttl(self)
    }
    fn set_ttl(&mut self, ttl: u8)
    where
        B: ByteSliceMut,
    {
        Ipv4Packet::set_ttl(self, ttl)
    }
}

impl<B: ByteSlice> IpPacket<B, Ipv6> for Ipv6Packet<B> {
    fn src_ip(&self) -> Ipv6Addr {
        Ipv6Packet::src_ip(self)
    }
    fn dst_ip(&self) -> Ipv6Addr {
        Ipv6Packet::dst_ip(self)
    }
    fn proto(&self) -> Result<IpProto, u8> {
        Ipv6Packet::proto(self)
    }
    fn ttl(&self) -> u8 {
        Ipv6Packet::hop_limit(self)
    }
    fn set_ttl(&mut self, ttl: u8)
    where
        B: ByteSliceMut,
    {
        Ipv6Packet::set_hop_limit(self, ttl)
    }
}
