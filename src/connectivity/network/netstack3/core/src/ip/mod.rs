// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Protocol, versions 4 and 6.

mod forwarding;
mod icmp;
mod igmp;
mod types;

// It's ok to `pub use` rather `pub(crate) use` here because the items in
// `types` which are themselves `pub(crate)` will still not be allowed to be
// re-exported from the root.
pub use self::types::*;

use log::{debug, trace};
use std::mem;

use packet::{
    BufferMut, BufferSerializer, MtuError, ParsablePacket, ParseBufferMut, ParseMetadata,
    Serializer,
};
use specialize_ip_macro::{specialize_ip, specialize_ip_address};

use crate::device::{DeviceId, FrameDestination};
use crate::error::ExistsError;
use crate::error::{IpParseError, NotFoundError};
use crate::ip::forwarding::{Destination, ForwardingTable};
use crate::wire::icmp::{Icmpv4ParameterProblem, Icmpv6ParameterProblem};
use crate::{Context, EventDispatcher};
use icmp::{
    send_icmpv4_parameter_problem, send_icmpv6_parameter_problem, should_send_icmpv4_error,
    should_send_icmpv6_error,
};

// default IPv4 TTL or IPv6 hops
const DEFAULT_TTL: u8 = 64;

// Minimum MTU required by all IPv6 devices.
pub(crate) const IPV6_MIN_MTU: u32 = 1280;

/// A builder for IP layer state.
#[derive(Default)]
pub struct IpStateBuilder {
    forward_v4: bool,
    forward_v6: bool,
}

impl IpStateBuilder {
    /// Enable or disable IP packet forwarding (default: disabled).
    ///
    /// If `forward` is true, then an incoming IP packet whose destination
    /// address identifies a remote host will be forwarded to that host.
    pub fn forward(&mut self, forward: bool) {
        self.forward_v4 = forward;
        self.forward_v6 = forward;
    }

    pub(crate) fn build(self) -> IpLayerState {
        IpLayerState {
            v4: IpLayerStateInner { forward: self.forward_v4, table: ForwardingTable::default() },
            v6: IpLayerStateInner { forward: self.forward_v6, table: ForwardingTable::default() },
        }
    }
}

/// The state associated with the IP layer.
pub(crate) struct IpLayerState {
    v4: IpLayerStateInner<Ipv4>,
    v6: IpLayerStateInner<Ipv6>,
}

struct IpLayerStateInner<I: Ip> {
    forward: bool,
    table: ForwardingTable<I>,
}

// TODO(joshlf): Once we support multiple extension headers in IPv6, we will
// need to verify that the callers of this function are still sound. In
// particular, they may accidentally pass a parse_metadata argument which
// corresponds to a single extension header rather than all of the IPv6 headers.

/// Dispatch a received IP packet to the appropriate protocol.
///
/// `device` is the device the packet was received on. `parse_metadata` is the
/// parse metadata associated with parsing the IP headers. It is used to undo
/// that parsing. Both `device` and `parse_metadata` are required in order to
/// send ICMP messages in response to unrecognized protocols or ports. If either
/// of `device` or `parse_metadata` is `None`, the caller promises that the
/// protocol and port are recognized.
///
/// # Panics
///
/// `dispatch_receive_ip_packet` panics if the protocol is unrecognized and
/// `parse_metadata` is `None`.
fn dispatch_receive_ip_packet<D: EventDispatcher, A: IpAddress, B: BufferMut>(
    ctx: &mut Context<D>,
    device: Option<DeviceId>,
    frame_dst: FrameDestination,
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    mut buffer: B,
    parse_metadata: Option<ParseMetadata>,
) {
    increment_counter!(ctx, "dispatch_receive_ip_packet");

    let res = match proto {
        IpProto::Icmp | IpProto::Icmpv6 => {
            icmp::receive_icmp_packet(ctx, device, src_ip, dst_ip, buffer);
            Ok(())
        }
        IpProto::Igmp => {
            igmp::receive_igmp_packet(ctx, src_ip, dst_ip, buffer);
            Ok(())
        }
        IpProto::Tcp => crate::transport::tcp::receive_ip_packet(ctx, src_ip, dst_ip, buffer),
        IpProto::Udp => crate::transport::udp::receive_ip_packet(ctx, src_ip, dst_ip, buffer),
        // A value of `IpProto::NoNextHeader` tells us that there is no header whatsoever
        // following the last lower-level header so we stop processing here.
        IpProto::NoNextHeader => Ok(()),
        IpProto::Other(_) => {
            // Undo the parsing of the IP packet header so that the buffer
            // contains the entire original IP packet.
            let meta = parse_metadata.unwrap();
            buffer.undo_parse(meta);
            icmp::send_icmp_protocol_unreachable(
                ctx,
                device.unwrap(),
                frame_dst,
                src_ip,
                dst_ip,
                proto,
                buffer,
                meta.header_len(),
            );
            Ok(())
        }
    };

    if let Err(mut buffer) = res {
        // TODO(joshlf): What if we're called from a loopback handler, and
        // device and parse_metadata are None? In other words, what happens if
        // we attempt to send to a loopback port which is unreachable? We will
        // eventually need to restructure the control flow here to handle that
        // case.

        // tcp::receive_ip_packet and udp::receive_ip_packet promise to return
        // the buffer in the same state it was in when they were called. Thus,
        // all we have to do is undo the parsing of the IP packet header, and
        // the buffer will be back to containing the entire original IP packet.
        let meta = parse_metadata.unwrap();
        buffer.undo_parse(meta);
        icmp::send_icmp_port_unreachable(
            ctx,
            device.unwrap(),
            frame_dst,
            src_ip,
            dst_ip,
            buffer,
            meta.header_len(),
        );
    }
}

/// Drop a packet and extract some of the fields.
///
/// `drop_packet!` saves the results of the `src_ip()`, `dst_ip()`, `proto()`,
/// and `parse_metadata()` methods, drops the packet, and returns them. This
/// exposes the buffer that the packet was borrowed from so that it can be used
/// directly again.
macro_rules! drop_packet {
    ($packet:expr) => {{
        let src_ip = $packet.src_ip();
        let dst_ip = $packet.dst_ip();
        let proto = $packet.proto();
        let meta = $packet.parse_metadata();
        mem::drop($packet);
        (src_ip, dst_ip, proto, meta)
    }};
}

/// Drop a packet and undo the effects of parsing it.
///
/// `drop_packet_and_undo_parse!` takes a `$packet` and a `$buffer` which the
/// packet was parsed from. It saves the results of the `src_ip()`, `dst_ip()`,
/// `proto()`, and `parse_metadata()` methods. It drops `$packet` and uses the
/// result of `parse_metadata()` to undo the effects of parsing the packet.
/// Finally, it returns the source IP, destination IP, protocol, and parse
/// metadata.
macro_rules! drop_packet_and_undo_parse {
    ($packet:expr, $buffer:expr) => {{
        let (src_ip, dst_ip, proto, meta) = drop_packet!($packet);
        $buffer.undo_parse(meta);
        (src_ip, dst_ip, proto, meta)
    }};
}

/// Receive an IP packet from a device.
///
/// `frame_dst` specifies whether this packet was received in a broadcast or
/// unicast link-layer frame.
pub(crate) fn receive_ip_packet<D: EventDispatcher, B: BufferMut, I: Ip>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    mut buffer: B,
) {
    trace!("receive_ip_packet({})", device);

    // Snapshot of `buffer`'s current state to revert to if parsing fails.
    // TODO(ghanan): Remove manual restoration of `buffer`'s state once
    //               the packet crate guarantees that if parsing fails,
    //               the buffer's state is left as it was before parsing.
    let p_len = buffer.prefix_len();
    let s_len = buffer.suffix_len();

    let result = buffer.parse_mut::<<I as IpExt<_>>::Packet>();

    if result.is_err() {
        let err = result.unwrap_err();

        // Revert `buffer` to it's original state.
        let n_p_len = buffer.prefix_len();
        let n_s_len = buffer.suffix_len();

        if p_len > n_p_len {
            buffer.grow_front(p_len - n_p_len);
        }

        if s_len > n_s_len {
            buffer.grow_back(s_len - n_s_len);
        }

        handle_parse_error(ctx, device, frame_dst, buffer, err);

        return;
    }

    // This will never panic because we check if `result` was an error earlier and return
    // before reaching here. That is, it is guaranteed that when we reach here, `result`
    // is not an error.
    let mut packet = result.unwrap();

    trace!("receive_ip_packet: parsed packet: {:?}", packet);

    // TODO(ghanan): For IPv4 packets, act upon options and for IPv6 packets,
    //               act upon extension headers.

    if I::LOOPBACK_SUBNET.contains(packet.dst_ip()) {
        // A packet from outside this host was sent with the destination IP of
        // the loopback address, which is illegal. Loopback traffic is handled
        // explicitly in send_ip_packet.
        //
        // TODO(joshlf): Do something with ICMP here?
        debug!("got packet from remote host for loopback address {}", packet.dst_ip());
    } else if deliver(ctx, device, packet.dst_ip()) {
        trace!("receive_ip_packet: delivering locally");
        // TODO(joshlf):
        // - Do something with ICMP if we don't have a handler for that protocol?
        // - Check for already-expired TTL?
        let (src_ip, dst_ip, proto, meta) = drop_packet!(packet);
        dispatch_receive_ip_packet(
            ctx,
            Some(device),
            frame_dst,
            src_ip,
            dst_ip,
            proto,
            buffer,
            Some(meta),
        );
    } else if let Some(dest) = forward(ctx, packet.dst_ip()) {
        let ttl = packet.ttl();
        if ttl > 1 {
            trace!("receive_ip_packet: forwarding");
            packet.set_ttl(ttl - 1);
            let (src_ip, dst_ip, proto, meta) = drop_packet_and_undo_parse!(packet, buffer);
            if let Err((err, ser)) = crate::device::send_ip_frame(
                ctx,
                dest.device,
                dest.next_hop,
                BufferSerializer::new_vec(buffer),
            ) {
                #[specialize_ip_address]
                fn send_packet_too_big<D: EventDispatcher, A: IpAddress, B: BufferMut>(
                    ctx: &mut Context<D>,
                    device: DeviceId,
                    frame_dst: FrameDestination,
                    src_ip: A,
                    dst_ip: A,
                    proto: IpProto,
                    mtu: u32,
                    original_packet: B,
                    header_len: usize,
                ) {
                    #[ipv6addr]
                    crate::ip::icmp::send_icmpv6_packet_too_big(
                        ctx,
                        device,
                        frame_dst,
                        src_ip,
                        dst_ip,
                        proto,
                        mtu,
                        original_packet,
                        header_len,
                    );
                }

                debug!("failed to forward IP packet: {:?}", err);
                if err.is_mtu() {
                    trace!("receive_ip_packet: Sending ICMPv6 Packet Too Big");
                    // TODO(joshlf): Increment the TTL since we just decremented
                    // it. The fact that we don't do this is technically a
                    // violation of the ICMP spec (we're not encapsulating the
                    // original packet that caused the issue, but a slightly
                    // modified version of it), but it's not that big of a deal
                    // because it won't affect the sender's ability to figure
                    // out the minimum path MTU. This may break other logic,
                    // though, so we should still fix it eventually.
                    let mtu = crate::device::get_mtu(ctx, device);
                    send_packet_too_big(
                        ctx,
                        device,
                        frame_dst,
                        src_ip,
                        dst_ip,
                        proto,
                        mtu,
                        ser.into_buffer(),
                        meta.header_len(),
                    );
                }
            }
        } else {
            // TTL is 0 or would become 0 after decrement; see "TTL" section,
            // https://tools.ietf.org/html/rfc791#page-14
            let (src_ip, dst_ip, proto, meta) = drop_packet_and_undo_parse!(packet, buffer);
            icmp::send_icmp_ttl_expired(
                ctx,
                device,
                frame_dst,
                src_ip,
                dst_ip,
                proto,
                buffer,
                meta.header_len(),
            );
            debug!("received IP packet dropped due to expired TTL");
        }
    } else {
        let (src_ip, dst_ip, proto, meta) = drop_packet_and_undo_parse!(packet, buffer);
        icmp::send_icmp_net_unreachable(
            ctx,
            device,
            frame_dst,
            src_ip,
            dst_ip,
            proto,
            buffer,
            meta.header_len(),
        );
        debug!("received IP packet with no known route to destination {}", dst_ip);
    }
}

/// Get the local address of the interface that will be used to route to a
/// remote address.
///
/// `local_address_for_remote` looks up the route to `remote`. If one is found,
/// it returns the IP address of the interface specified by the route, or `None`
/// if the interface has no IP address.
pub(crate) fn local_address_for_remote<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    remote: A,
) -> Option<A> {
    let route = lookup_route(&ctx.state().ip, remote)?;
    crate::device::get_ip_addr_subnet(ctx, route.device).map(AddrSubnet::into_addr)
}

/// Handle an IP parsing error, `err`.
#[specialize_ip]
fn handle_parse_error<D: EventDispatcher, I: Ip, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    original_packet: B,
    err: IpParseError<I>,
) {
    match err {
        // Conditionally send an ICMP response if we encountered a parameter
        // problem error when parsing an IP packet. Note, we do not always send
        // back an ICMP response as it can be used as an attack vector for DDoS
        // attacks. We only send back an ICMP response if the RFC requires
        // that we MUST send one, as noted by `must_send_icmp` and `action`.
        IpParseError::ParameterProblem {
            src_ip,
            dst_ip,
            code,
            pointer,
            must_send_icmp,
            header_len,
            action,
        } => {
            if action.should_send_icmp(&dst_ip) && must_send_icmp {
                #[ipv4]
                {
                    // This should never return `true` for IPv4.
                    assert!(!action.should_send_icmp_to_multicast());

                    if should_send_icmpv4_error(frame_dst, src_ip, dst_ip) {
                        send_icmpv4_parameter_problem(
                            ctx,
                            device,
                            src_ip,
                            dst_ip,
                            code,
                            Icmpv4ParameterProblem::new(pointer),
                            original_packet,
                            header_len,
                        );
                    }
                }

                #[ipv6]
                {
                    // Some IPv6 parsing errors may require us to send an
                    // ICMP response even if the original packet's destination
                    // was a multicast (as defined by RFC 4443 section 2.4.e).
                    // `action.should_send_icmp_to_multicast()` should return
                    // `true` if such an exception applies.
                    if should_send_icmpv6_error(
                        frame_dst,
                        src_ip,
                        dst_ip,
                        action.should_send_icmp_to_multicast(),
                    ) {
                        send_icmpv6_parameter_problem(
                            ctx,
                            device,
                            src_ip,
                            dst_ip,
                            code,
                            Icmpv6ParameterProblem::new(pointer),
                            original_packet,
                            header_len,
                        );
                    }
                }
            }
        }
        // TODO(joshlf): Do something with ICMP here?
        _ => {}
    }
}

// Should we deliver this packet locally?
// deliver returns true if:
// - dst_ip is equal to the address set on the device
// - dst_ip is equal to the broadcast address of the subnet set on the device
// - dst_ip is equal to the global broadcast address
#[specialize_ip_address]
fn deliver<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device: DeviceId,
    dst_ip: A,
) -> bool {
    // TODO(joshlf):
    // - This implements a strict host model (in which we only accept packets
    //   which are addressed to the device over which they were received). This
    //   is the easiest to implement for the time being, but we should actually
    //   put real thought into what our host model should be (NET-1011).
    #[ipv4addr]
    return crate::device::get_ip_addr_subnet(ctx, device)
        .map(AddrSubnet::into_addr_subnet)
        .map(|(addr, subnet)| dst_ip == addr || dst_ip == subnet.broadcast())
        .unwrap_or(dst_ip.is_global_broadcast());
    // TODO(brunodalbo):
    // Along with the host model described above, we need to be able to have
    // multiple IPs per interface, it becomes imperative for IPv6.
    //
    // Also, only when we're a router we should accept
    // Ipv6::ALL_ROUTERS_LINK_LOCAL.
    #[ipv6addr]
    return crate::device::get_ip_addr_subnet(ctx, device)
        .map(AddrSubnet::into_addr_subnet)
        .map(|(addr, _): (Ipv6Addr, _)| addr.destination_matches(&dst_ip))
        .unwrap_or(false)
        || crate::device::get_ipv6_link_local_addr(ctx, device).destination_matches(&dst_ip)
        || dst_ip == Ipv6::ALL_NODES_LINK_LOCAL_ADDRESS;
}

// Should we forward this packet, and if so, to whom?
#[specialize_ip_address]
fn forward<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    dst_ip: A,
) -> Option<Destination<A::Version>> {
    let ip_state = &ctx.state().ip;

    #[ipv4addr]
    let forward = ip_state.v4.forward;
    #[ipv6addr]
    let forward = ip_state.v6.forward;

    if forward {
        lookup_route(ip_state, dst_ip)
    } else {
        None
    }
}

// Look up the route to a host.
#[specialize_ip_address]
fn lookup_route<A: IpAddress>(state: &IpLayerState, dst_ip: A) -> Option<Destination<A::Version>> {
    #[ipv4addr]
    return state.v4.table.lookup(dst_ip);
    #[ipv6addr]
    return state.v6.table.lookup(dst_ip);
}

/// Add a route to the forwarding table, returning `Err` if the subnet
/// is already in the table.
#[specialize_ip_address]
pub(crate) fn add_route<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    subnet: Subnet<A>,
    next_hop: A,
) -> Result<(), ExistsError> {
    let state = &mut ctx.state_mut().ip;

    #[ipv4addr]
    return state.v4.table.add_route(subnet, next_hop);
    #[ipv6addr]
    return state.v6.table.add_route(subnet, next_hop);
}

/// Add a device route to the forwarding table, returning `Err` if the
/// subnet is already in the table.
#[specialize_ip_address]
pub(crate) fn add_device_route<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    subnet: Subnet<A>,
    device: DeviceId,
) -> Result<(), ExistsError> {
    let state = &mut ctx.state_mut().ip;

    #[ipv4addr]
    return state.v4.table.add_device_route(subnet, device);
    #[ipv6addr]
    return state.v6.table.add_device_route(subnet, device);
}

/// Delete a route from the forwarding table, returning `Err` if no
/// route was found to be deleted.
#[specialize_ip_address]
pub(crate) fn del_device_route<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    subnet: Subnet<A>,
) -> Result<(), NotFoundError> {
    let state = &mut ctx.state_mut().ip;

    #[ipv4addr]
    return state.v4.table.del_route(subnet);
    #[ipv6addr]
    return state.v6.table.del_route(subnet);
}

/// Return the routes for the provided `IpAddress` type
#[specialize_ip_address]
pub(crate) fn iter_routes<D: EventDispatcher, I: IpAddress>(
    ctx: &Context<D>,
) -> std::slice::Iter<Entry<I>> {
    let state = &ctx.state().ip;
    #[ipv4addr]
    return state.v4.table.iter_routes();
    #[ipv6addr]
    return state.v6.table.iter_routes();
}

/// Is this one of our local addresses?
///
/// `is_local_addr` returns whether `addr` is the address associated with one of
/// our local interfaces.
pub(crate) fn is_local_addr<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    addr: A,
) -> bool {
    log_unimplemented!(false, "ip::is_local_addr: not implemented")
}

/// Send an IP packet to a remote host.
///
/// `send_ip_packet` accepts a destination IP address, a protocol, and a
/// callback. It computes the routing information, and invokes the callback with
/// the computed destination address. The callback returns a
/// `SerializationRequest`, which is serialized in a new IP packet and sent.
pub(crate) fn send_ip_packet<D: EventDispatcher, A, S, F>(
    ctx: &mut Context<D>,
    dst_ip: A,
    proto: IpProto,
    get_body: F,
) -> Result<(), (MtuError<S::InnerError>, S)>
where
    A: IpAddress,
    S: Serializer,
    F: FnOnce(A) -> S,
{
    trace!("send_ip_packet({}, {})", dst_ip, proto);
    increment_counter!(ctx, "send_ip_packet");
    if A::Version::LOOPBACK_SUBNET.contains(dst_ip) {
        increment_counter!(ctx, "send_ip_packet::loopback");

        // TODO(joshlf): Currently, we have no way of representing the loopback
        // device as a DeviceId. We will need to fix that eventually.

        // TODO(joshlf): Currently, we serialize using the normal Serializer
        // functionality. I wonder if, in the case of delivering to loopback, we
        // can do something more efficient?
        let mut buffer = get_body(A::Version::LOOPBACK_ADDRESS)
            .serialize_outer()
            .map_err(|(err, ser)| (err.into(), ser))?;
        // TODO(joshlf): Respond with some kind of error if we don't have a
        // handler for that protocol? Maybe simulate what would have happened
        // (w.r.t ICMP) if this were a remote host?

        // NOTE(joshlf): By passing a DeviceId and ParseMetadata of None here,
        // we are promising that the protocol will be recognized (and the call
        // will panic if it's not). This is OK because the fact that we're
        // sending a packet with this protocol means we are able to process that
        // protocol.
        //
        // NOTE(joshlf): By doing that, we are also promising that the port will
        // be recognized. That is NOT OK, and the call will panic in that case.
        // TODO(joshlf): Fix this.
        dispatch_receive_ip_packet(
            ctx,
            None,
            FrameDestination::Unicast,
            A::Version::LOOPBACK_ADDRESS,
            dst_ip,
            proto,
            buffer.as_buf_mut(),
            None,
        );
    } else if let Some(dest) = lookup_route(&ctx.state().ip, dst_ip) {
        // TODO(joshlf): Are we sure that a device route can never be set for a
        // device without an IP address? At the least, this is not currently
        // enforced anywhere, and is a DoS vector.
        let src_ip = crate::device::get_ip_addr_subnet(ctx, dest.device)
            .expect("IP device route set for device without IP address")
            .addr();
        send_ip_packet_from_device(
            ctx,
            dest.device,
            src_ip,
            dst_ip,
            dest.next_hop,
            proto,
            get_body(src_ip),
        )?;
    } else {
        debug!("No route to host");
        // TODO(joshlf): No route to host
    }

    Ok(())
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
pub(crate) fn send_ip_packet_from<D: EventDispatcher, A, S>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    body: S,
) -> Result<(), (MtuError<S::InnerError>, S)>
where
    A: IpAddress,
    S: Serializer,
{
    // TODO(joshlf): Figure out how to compute a route with the restrictions
    // mentioned in the doc comment.
    log_unimplemented!(Ok(()), "ip::send_ip_packet_from: not implemented")
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
pub(crate) fn send_ip_packet_from_device<D: EventDispatcher, A, S>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: A,
    dst_ip: A,
    next_hop: A,
    proto: IpProto,
    body: S,
) -> Result<(), (MtuError<S::InnerError>, S)>
where
    A: IpAddress,
    S: Serializer,
{
    assert!(!A::Version::LOOPBACK_SUBNET.contains(src_ip));
    assert!(!A::Version::LOOPBACK_SUBNET.contains(dst_ip));

    let body = body.encapsulate(<A::Version as IpExt<&[u8]>>::PacketBuilder::new(
        src_ip,
        dst_ip,
        DEFAULT_TTL,
        proto,
    ));
    crate::device::send_ip_frame(ctx, device, next_hop, body)
        .map_err(|(err, ser)| (err, ser.into_serializer()))
}

/// Send an ICMP response to a remote host.
///
/// Unlike other send functions, `send_icmp_response` takes the ingress device,
/// source IP, and destination IP of the packet *being responded to*. It uses
/// ICMP-specific logic to figure out whether and how to send an ICMP response.
/// `get_body` returns a `Serializer` with the bytes of the ICMP packet.
fn send_icmp_response<D: EventDispatcher, A, S, F>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    get_body: F,
) -> Result<(), (MtuError<S::InnerError>, S)>
where
    A: IpAddress,
    S: Serializer,
    F: FnOnce(A) -> S,
{
    trace!("send_icmp_response({}, {}, {}, {})", device, src_ip, dst_ip, proto);
    increment_counter!(ctx, "send_icmp_response");

    // TODO(joshlf): Should this be used for ICMP echo replies as well?

    // TODO(joshlf): Come up with rules for when to send ICMP responses. E.g.,
    // should we send a response over a different device than the device that
    // the original packet ingressed over? We'll probably want to consult BCP 38
    // (aka RFC 2827) and RFC 3704.

    let ip_state = &mut ctx.state_mut().ip;
    if let Some(route) = lookup_route(ip_state, src_ip) {
        if let Some(local_ip) =
            crate::device::get_ip_addr_subnet(ctx, route.device).map(AddrSubnet::into_addr)
        {
            send_ip_packet_from_device(
                ctx,
                route.device,
                local_ip,
                src_ip,
                route.next_hop,
                proto,
                get_body(local_ip),
            )?;
        } else {
            log_unimplemented!(
                (),
                "Sending ICMP over unnumbered device {} is unimplemented",
                route.device
            );

            // TODO(joshlf): We need a general-purpose mechanism for choosing a
            // source address in cases where we're a) acting as a router (and
            // thus sending packets with our own source address, but not as a
            // result of any local application behavior) and, b) sending over an
            // unnumbered device (one without any configured IP address). ICMP
            // is the notable use case. Most likely, we will want to pick the IP
            // address of a different local device. See for an explanation of
            // why we might have this setup:
            // https://www.cisco.com/c/en/us/support/docs/ip/hot-standby-router-protocol-hsrp/13786-20.html#unnumbered_iface
        }
    } else {
        debug!("Can't send ICMP response to {}: no route to host", src_ip);
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    use byteorder::{ByteOrder, NetworkEndian};
    use packet::{Buf, ParseBuffer};

    use crate::device::FrameDestination;
    use crate::testutil::*;
    use crate::wire::ethernet::EthernetFrame;
    use crate::wire::icmp::{IcmpIpExt, IcmpParseArgs, Icmpv6Packet, Icmpv6ParameterProblemCode};
    use crate::wire::ipv6::ext_hdrs::ExtensionHeaderOptionAction;
    use crate::DeviceId;

    //
    // Some helper functions
    //

    /// Get the counter value for a `key`.
    fn get_counter_val(ctx: &mut Context<DummyEventDispatcher>, key: &str) -> usize {
        *ctx.state.test_counters.get(key)
    }

    /// Verify that an ICMP Parameter Problem packet was actually sent in response to
    /// a packet with an unrecognized IPv6 extension header option.
    ///
    /// `verify_icmp_for_unrecognized_ext_hdr_option` verifies that the next frame
    /// in `net` is an ICMP packet with code set to `code`, and pointer set to `pointer`.
    fn verify_icmp_for_unrecognized_ext_hdr_option(
        ctx: &mut Context<DummyEventDispatcher>,
        code: Icmpv6ParameterProblemCode,
        pointer: u32,
    ) {
        // Check the ICMP that bob attempted to send to alice
        let device_frames = ctx.dispatcher.frames_sent().clone();
        assert!(!device_frames.is_empty());
        let mut buffer = Buf::new(device_frames[0].1.as_slice(), ..);
        let frame = buffer.parse::<EthernetFrame<_>>().unwrap();
        let packet = buffer.parse::<<Ipv6 as IpExt<&[u8]>>::Packet>().unwrap();
        let (src_ip, dst_ip, proto, _) = drop_packet!(packet);
        assert_eq!(dst_ip, DUMMY_CONFIG_V6.remote_ip);
        assert_eq!(src_ip, DUMMY_CONFIG_V6.local_ip);
        assert_eq!(proto, IpProto::Icmpv6);
        let icmp = buffer
            .parse_with::<_, <Ipv6 as IcmpIpExt<&[u8]>>::Packet>(IcmpParseArgs::new(src_ip, dst_ip))
            .unwrap();
        if let Icmpv6Packet::ParameterProblem(icmp) = icmp {
            assert_eq!(icmp.code(), code);
            assert_eq!(icmp.message().pointer(), pointer);
        } else {
            panic!("Expected ICMPv6 Parameter Problem: {:?}", icmp);
        }
    }

    /// Populate a buffer `bytes` with data required to test unrecognized options.
    ///
    /// The unrecognized option type will be located at index 48. `bytes` must be
    /// at least 64 bytes long. If `to_multicast` is `true`, the destination address
    /// of the packet will be a multicast address.
    fn buf_for_unrecognized_ext_hdr_option_test(
        bytes: &mut [u8],
        action: ExtensionHeaderOptionAction,
        to_multicast: bool,
    ) -> Buf<&mut [u8]> {
        assert!(bytes.len() >= 64);

        let action: u8 = action.into();

        // Unrecognized Option type.
        let oty = 63 | (action << 6);

        #[rustfmt::skip]
        bytes[40..64].copy_from_slice(&[
            // Destination Options Extension Header
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                        // Pad1
            1,   0,                   // Pad2
            1,   1, 0,                // Pad3
            oty, 6, 0, 0, 0, 0, 0, 0, // Unrecognized type w/ action = discard

            // Body
            1, 2, 3, 4, 5, 6, 7, 8
        ][..]);
        bytes[..4].copy_from_slice(&[0x60, 0x20, 0x00, 0x77][..]);

        let payload_len = (bytes.len() - 40) as u16;
        NetworkEndian::write_u16(&mut bytes[4..6], payload_len);

        bytes[6] = Ipv6ExtHdrType::DestinationOptions.into();
        bytes[7] = 64;
        bytes[8..24].copy_from_slice(DUMMY_CONFIG_V6.remote_ip.bytes());

        if to_multicast {
            bytes[24..40].copy_from_slice(
                &[255, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32][..],
            );
        } else {
            bytes[24..40].copy_from_slice(DUMMY_CONFIG_V6.local_ip.bytes());
        }

        Buf::new(bytes, ..)
    }

    #[test]
    fn test_ipv6_icmp_parameter_problem_non_must() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V6)
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(1);

        //
        // Test parsing an IPv6 packet with invalid next header value which
        // we SHOULD send an ICMP response for (but we don't since its not a
        // MUST).
        //

        #[rustfmt::skip]
        let bytes: &mut [u8] = &mut [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // Body
            1, 2, 3, 4, 5,
        ][..];
        bytes[..4].copy_from_slice(&[0x60, 0x20, 0x00, 0x77][..]);
        let payload_len = (bytes.len() - 40) as u16;
        NetworkEndian::write_u16(&mut bytes[4..6], payload_len);
        bytes[6] = 255; // Invalid Next Header
        bytes[7] = 64;
        bytes[8..24].copy_from_slice(DUMMY_CONFIG_V6.remote_ip.bytes());
        bytes[24..40].copy_from_slice(DUMMY_CONFIG_V6.local_ip.bytes());
        let mut buf = Buf::new(bytes, ..);

        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, FrameDestination::Unicast, buf);

        assert_eq!(get_counter_val(&mut ctx, "send_icmpv4_parameter_problem"), 0);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), 0);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 0);
    }

    #[test]
    fn test_ipv6_icmp_parameter_problem_must() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V6)
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(1);

        //
        // Test parsing an IPv6 packet where we MUST send an ICMP parameter problem
        // response (invalid routing type for a routing extension header).
        //

        #[rustfmt::skip]
        let bytes: &mut [u8] = &mut [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // Routing Extension Header
            IpProto::Tcp.into(),         // Next Header
            4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            255,                                // Routing Type (Invalid)
            1,                                  // Segments Left
            0, 0, 0, 0,                         // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            // Body
            1, 2, 3, 4, 5,
        ][..];
        bytes[..4].copy_from_slice(&[0x60, 0x20, 0x00, 0x77][..]);
        let payload_len = (bytes.len() - 40) as u16;
        NetworkEndian::write_u16(&mut bytes[4..6], payload_len);
        bytes[6] = Ipv6ExtHdrType::Routing.into();
        bytes[7] = 64;
        bytes[8..24].copy_from_slice(DUMMY_CONFIG_V6.remote_ip.bytes());
        bytes[24..40].copy_from_slice(DUMMY_CONFIG_V6.local_ip.bytes());
        let mut buf = Buf::new(bytes, ..);
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, FrameDestination::Unicast, buf);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::ErroneousHeaderField,
            42,
        );
    }

    #[test]
    fn test_ipv6_unrecognized_ext_hdr_option() {
        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V6)
            .build::<DummyEventDispatcher>();
        let device = DeviceId::new_ethernet(1);
        let mut expected_icmps = 0;
        let mut bytes = [0; 64];
        let frame_dst = FrameDestination::Unicast;

        //
        // Test parsing an IPv6 packet where we MUST send an ICMP parameter problem
        // due to an unrecognized extension header option.
        //

        //
        // Test with unrecognized option type set with
        // action = discard.
        //

        let mut buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacket,
            false,
        );
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);

        //
        // Test with unrecognized option type set with
        // action = discard & send icmp
        // where dest addr is a unicast addr.
        //

        let mut buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendICMP,
            false,
        );
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
            48,
        );

        //
        // Test with unrecognized option type set with
        // action = discard & send icmp
        // where dest addr is a multicast addr.
        //

        let mut buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendICMP,
            true,
        );
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
            48,
        );

        //
        // Test with unrecognized option type set with
        // action = discard & send icmp if not multicast addr
        // where dest addr is a unicast addr.
        //

        let mut buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast,
            false,
        );
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);
        expected_icmps += 1;
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);
        verify_icmp_for_unrecognized_ext_hdr_option(
            &mut ctx,
            Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
            48,
        );

        //
        // Test with unrecognized option type set with
        // action = discard & send icmp if not multicast addr
        // but dest addr is a multicast addr.
        //

        let mut buf = buf_for_unrecognized_ext_hdr_option_test(
            &mut bytes,
            ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast,
            true,
        );
        // Do not expect an ICMP response for this packet
        receive_ip_packet::<_, _, Ipv6>(&mut ctx, device, frame_dst, buf);
        assert_eq!(get_counter_val(&mut ctx, "send_icmpv6_parameter_problem"), expected_icmps);

        //
        // None of our tests should have sent an icmpv4 packet or dispatched an ip packet.
        //

        assert_eq!(get_counter_val(&mut ctx, "send_icmpv4_parameter_problem"), 0);
        assert_eq!(get_counter_val(&mut ctx, "dispatch_receive_ip_packet"), 0);
    }
}
