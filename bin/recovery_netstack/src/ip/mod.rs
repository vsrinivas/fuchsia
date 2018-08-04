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

use std::fmt::{self, Debug, Formatter};
use std::mem;
use std::ops::Range;

use device::DeviceId;
use error::ParseError;
use ip::forwarding::{Destination, ForwardingTable};
use wire::ipv4::{Ipv4Packet, Ipv4PacketBuilder};
use wire::ipv6::{Ipv6Packet, Ipv6PacketBuilder};
use wire::{ensure_prefix_padding, AddrSerializationCallback, BufferAndRange, SerializationCallback};
use zerocopy::ByteSlice;
use StackState;

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

fn dispatch_receive_ip_packet<I: IpAddr, B: AsRef<[u8]> + AsMut<[u8]>>(
    proto: IpProto, state: &mut StackState, src_ip: I, dst_ip: I, mut buffer: BufferAndRange<B>,
) -> bool {
    increment_counter!(state, "dispatch_receive_ip_packet");
    match proto {
        IpProto::Icmp => icmp::receive_icmp_packet(state, src_ip, dst_ip, buffer),
        _ => ::transport::receive_ip_packet(state, src_ip, dst_ip, proto, buffer),
    }
}

/// Receive an IP packet from a device.
pub fn receive_ip_packet<I: Ip>(
    state: &mut StackState, device: DeviceId, mut buffer: BufferAndRange<&mut [u8]>,
) {
    trace!("receive_ip_packet({})", device);
    let (mut packet, body_range) =
        if let Ok((packet, body_range)) = <I as IpExt>::Packet::parse(buffer.as_mut()) {
            (packet, body_range)
        } else {
            // TODO(joshlf): Do something with ICMP here?
            return;
        };
    trace!("receive_ip_packet: parsed packet: {:?}", packet);

    println!(
        "received IP packet from={:?} to={:?}",
        packet.src_ip(),
        packet.dst_ip()
    );
    if I::LOOPBACK_SUBNET.contains(packet.dst_ip()) {
        // A packet from outside this host was sent with the destination IP of
        // the loopback address, which is illegal. Loopback traffic is handled
        // explicitly in send_ip_packet. TODO(joshlf): Do something with ICMP
        // here?
        debug!(
            "got packet from remote host for loopback address {}",
            packet.dst_ip()
        );
    } else if deliver(state, device, packet.dst_ip()) {
        trace!("receive_ip_packet: delivering locally");
        // TODO(joshlf):
        // - Do something with ICMP if we don't have a handler for that protocol?
        // - Check for already-expired TTL?
        let handled = if let Ok(proto) = packet.proto() {
            let src_ip = packet.src_ip();
            let dst_ip = packet.dst_ip();
            // drop packet so we can re-use the underlying buffer
            mem::drop(packet);
            // slice the buffer to be only the body range
            buffer.slice(body_range);
            dispatch_receive_ip_packet(proto, state, src_ip, dst_ip, buffer)
        } else {
            // TODO(joshlf): Log unrecognized protocol number
            false
        };
    } else if let Some(dest) = forward(state, packet.dst_ip()) {
        let ttl = packet.ttl();
        if ttl > 1 {
            trace!("receive_ip_packet: forwarding");
            packet.set_ttl(ttl - 1);
            // drop packet so we can re-use the underlying buffer
            mem::drop(packet);
            ::device::send_ip_frame(
                state,
                dest.device,
                dest.next_hop,
                |prefix_bytes, body_plus_padding_bytes| {
                    // The current buffer may not have enough prefix space for
                    // all of the link-layer headers or for the post-body
                    // padding, so use ensure_prefix_padding to ensure that we
                    // are using a buffer with sufficient space.
                    ensure_prefix_padding(buffer, prefix_bytes, body_plus_padding_bytes)
                },
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
        debug!(
            "received IP packet with no known route to destination {}",
            packet.dst_ip()
        );
    }
}

// Should we deliver this packet locally?
// deliver returns true if:
// - dst_ip is equal to the address set on the device
// - dst_ip is equal to the broadcast address of the subnet set on the device
// - dst_ip is equal to the global broadcast address
fn deliver<A: IpAddr>(state: &mut StackState, device: DeviceId, dst_ip: A) -> bool {
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
    A::deliver(dst_ip, ::device::get_ip_addr::<A>(state, device))
}

// Should we forward this packet, and if so, to whom?
fn forward<A: IpAddr>(state: &mut StackState, dst_ip: A) -> Option<Destination<A::Version>> {
    specialize_ip_addr!(
        fn forwarding_enabled(state: &IpLayerState) -> bool {
            Ipv4Addr => { state.v4.forward }
            Ipv6Addr => { state.v6.forward }
        }
    );
    if A::forwarding_enabled(&state.ip) {
        lookup_route(&state.ip, dst_ip)
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

/// Send an IP packet to a remote host.
///
/// `send_ip_packet` accepts a destination IP address, a protocol, and a
/// callback. It computes the routing information and invokes the callback with
/// the source address and the number of prefix bytes required by all
/// encapsulating headers, and the minimum size of the body plus padding. The
/// callback is expected to return a byte buffer and a range which corresponds
/// to the desired body to be encapsulated. The portion of the buffer beyond the
/// end of the body range will be treated as padding. The total number of bytes
/// in the body and the post-body padding must not be smaller than the minimum
/// size passed to the callback.
///
/// For more details on the callback, see the
/// [`::wire::AddrSerializationCallback`] documentation.
///
/// # Panics
///
/// `send_ip_packet` panics if the buffer returned from `get_buffer` does not
/// have sufficient space preceding the body for all encapsulating headers or
/// does not have enough body plus padding bytes to satisfy the requirement
/// passed to the callback.
pub fn send_ip_packet<A, B, F>(state: &mut StackState, dst_ip: A, proto: IpProto, get_buffer: F)
where
    A: IpAddr,
    B: AsRef<[u8]> + AsMut<[u8]>,
    F: AddrSerializationCallback<A, B>,
{
    trace!("send_ip_packet({}, {})", dst_ip, proto);
    increment_counter!(state, "send_ip_packet");
    if A::Version::LOOPBACK_SUBNET.contains(dst_ip) {
        increment_counter!(state, "send_ip_packet::loopback");
        let buffer = get_buffer(A::Version::LOOPBACK_ADDRESS, 0, 0);
        // TODO(joshlf): Respond with some kind of error if we don't have a
        // handler for that protocol? Maybe simulate what would have happened
        // (w.r.t ICMP) if this were a remote host?
        dispatch_receive_ip_packet(proto, state, A::Version::LOOPBACK_ADDRESS, dst_ip, buffer);
    } else if let Some(dest) = lookup_route(&state.ip, dst_ip) {
        let (src_ip, _) = ::device::get_ip_addr(state, dest.device)
            .expect("IP device route set for device without IP address");
        send_ip_packet_from(
            state,
            dest.device,
            src_ip,
            dst_ip,
            dest.next_hop,
            proto,
            |prefix_bytes, body_plus_padding_bytes| {
                get_buffer(src_ip, prefix_bytes, body_plus_padding_bytes)
            },
        );
    } else {
        println!("No route to host");
        // TODO(joshlf): No route to host
    }
}

/// Send an IP packet to a remote host over a specific device.
///
/// `send_ip_packet_from` accepts a device, a source and destination IP address,
/// a next hop IP address, and a callback. It invokes the callback with the
/// number of prefix bytes required by all encapsulating headers, and the
/// minimum size of the body plus padding. The callback is expected to return a
/// byte buffer and a range which corresponds to the desired body to be
/// encapsulated. The portion of the buffer beyond the end of the body range
/// will be treated as padding. The total number of bytes in the body and the
/// post-body padding must not be smaller than the minimum size passed to the
/// callback.
///
/// For more details on the callback, see the [`::wire::SerializationCallback`]
/// documentation.
///
/// # Panics
///
/// `send_ip_packet_from` panics if the buffer returned from `get_buffer` does
/// not have sufficient space preceding the body for all encapsulating headers
/// or does not have enough body plus padding bytes to satisfy the requirement
/// passed to the callback.
///
/// Since `send_ip_packet_from` specifies a physical device, it cannot send to
/// or from a loopback IP address. If either `src_ip` or `dst_ip` are in the
/// loopback subnet, `send_ip_packet_from` will panic.
pub fn send_ip_packet_from<A, B, F>(
    state: &mut StackState, device: DeviceId, src_ip: A, dst_ip: A, next_hop: A, proto: IpProto,
    get_buffer: F,
) where
    A: IpAddr,
    B: AsRef<[u8]> + AsMut<[u8]>,
    F: SerializationCallback<B>,
{
    assert!(!A::Version::LOOPBACK_SUBNET.contains(src_ip));
    assert!(!A::Version::LOOPBACK_SUBNET.contains(dst_ip));
    ::device::send_ip_frame(
        state,
        device,
        next_hop,
        |mut prefix_bytes, mut body_plus_padding_bytes| {
            prefix_bytes += max_header_len::<A::Version>();
            body_plus_padding_bytes -= min_header_len::<A::Version>();
            let buffer = get_buffer(prefix_bytes, body_plus_padding_bytes);
            serialize_packet(src_ip, dst_ip, DEFAULT_TTL, proto, buffer)
        },
    );
}

// The maximum header length for a given IP packet format.
fn max_header_len<I: Ip>() -> usize {
    specialize_ip!(
        fn max_header_len() -> usize {
            Ipv4 => { ::wire::ipv4::MAX_HEADER_LEN }
            Ipv6 => { log_unimplemented!(60, "ip::max_header_len: Ipv6 not implemented") }
        }
    );
    I::max_header_len()
}

// The minimum header length for a given IP packet format.
fn min_header_len<I: Ip>() -> usize {
    specialize_ip!(
        fn min_header_len() -> usize {
            Ipv4 => { ::wire::ipv4::MIN_HEADER_LEN }
            Ipv6 => { ::wire::ipv6::MIN_HEADER_LEN }
        }
    );
    I::min_header_len()
}

// Serialize an IP packet into the provided buffer, returning the byte range
// within the buffer corresponding to the serialized packet.
fn serialize_packet<A: IpAddr, B: AsMut<[u8]>>(
    src_ip: A, dst_ip: A, ttl: u8, proto: IpProto, buffer: BufferAndRange<B>,
) -> BufferAndRange<B> {
    // serialize_ip! can't handle trait bounds with type arguments, so create
    // AsMutU8 which is equivalent to AsMut<[u8]>, but without the type
    // arguments. Ew.
    trait AsMutU8: AsMut<[u8]> {}
    impl<A: AsMut<[u8]>> AsMutU8 for A {}
    specialize_ip!(
        fn serialize<B>(
            src_ip: Self::Addr, dst_ip: Self::Addr, ttl: u8, proto: IpProto, buffer: BufferAndRange<B>
        ) -> BufferAndRange<B>
        where
            B: AsMutU8,
        {
            Ipv4 => { Ipv4PacketBuilder::new(src_ip, dst_ip, ttl, proto).serialize(buffer) }
            Ipv6 => { Ipv6PacketBuilder::new(src_ip, dst_ip, ttl, proto).serialize(buffer) }
        }
    );
    A::Version::serialize(src_ip, dst_ip, ttl, proto, buffer)
}

// An `Ip` extension trait for internal use.
//
// This trait adds extra associated types that are useful for our implementation
// here, but which consumers outside of the ip module do not need to see.
trait IpExt<'a>: Ip {
    type Packet: IpPacket<'a, Self>;
}

impl<'a, I: Ip> IpExt<'a> for I {
    default type Packet = !;
}

impl<'a> IpExt<'a> for Ipv4 {
    type Packet = Ipv4Packet<&'a mut [u8]>;
}

impl<'a> IpExt<'a> for Ipv6 {
    type Packet = Ipv6Packet<&'a mut [u8]>;
}

// `Ipv4Packet` or `Ipv6Packet`
trait IpPacket<'a, I: Ip>: Sized + Debug {
    fn parse(bytes: &'a mut [u8]) -> Result<(Self, Range<usize>), ParseError>;
    fn src_ip(&self) -> I::Addr;
    fn dst_ip(&self) -> I::Addr;
    fn proto(&self) -> Result<IpProto, u8>;
    fn ttl(&self) -> u8;
    fn set_ttl(&mut self, ttl: u8);
}

impl<'a> IpPacket<'a, Ipv4> for Ipv4Packet<&'a mut [u8]> {
    fn parse(bytes: &'a mut [u8]) -> Result<(Ipv4Packet<&'a mut [u8]>, Range<usize>), ParseError> {
        Ipv4Packet::parse(bytes)
    }
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
    fn set_ttl(&mut self, ttl: u8) {
        Ipv4Packet::set_ttl(self, ttl)
    }
}

impl<'a> IpPacket<'a, Ipv6> for Ipv6Packet<&'a mut [u8]> {
    fn parse(bytes: &'a mut [u8]) -> Result<(Ipv6Packet<&'a mut [u8]>, Range<usize>), ParseError> {
        Ipv6Packet::parse(bytes)
    }
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
    fn set_ttl(&mut self, ttl: u8) {
        Ipv6Packet::set_hop_limit(self, ttl)
    }
}
