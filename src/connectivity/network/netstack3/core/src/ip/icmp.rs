// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Control Message Protocol (ICMP).

use std::mem;

use log::trace;
use packet::{BufferMut, BufferSerializer, Serializer};
use specialize_ip_macro::specialize_ip_address;

use crate::device::{ndp, DeviceId, FrameDestination};
use crate::ip::{
    send_icmp_response, send_ip_packet, IpAddress, IpProto, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr,
    IPV6_MIN_MTU,
};
use crate::wire::icmp::{
    peek_message_type, IcmpDestUnreachable, IcmpIpExt, IcmpIpTypes, IcmpMessageType,
    IcmpPacketBuilder, IcmpParseArgs, IcmpTimeExceeded, IcmpUnusedCode, Icmpv4DestUnreachableCode,
    Icmpv4MessageType, Icmpv4Packet, Icmpv4ParameterProblem, Icmpv4ParameterProblemCode,
    Icmpv4TimeExceededCode, Icmpv6DestUnreachableCode, Icmpv6MessageType, Icmpv6Packet,
    Icmpv6PacketTooBig, Icmpv6ParameterProblem, Icmpv6ParameterProblemCode, Icmpv6TimeExceededCode,
};
use crate::{Context, EventDispatcher};

/// Receive an ICMP message in an IP packet.
#[specialize_ip_address]
pub(crate) fn receive_icmp_packet<D: EventDispatcher, A: IpAddress, B: BufferMut>(
    ctx: &mut Context<D>,
    device: Option<DeviceId>,
    src_ip: A,
    dst_ip: A,
    mut buffer: B,
) {
    trace!("receive_icmp_packet({}, {})", src_ip, dst_ip);

    let packet = match buffer.parse_with::<_, <A::Version as IcmpIpExt<&[u8]>>::Packet>(
        IcmpParseArgs::new(src_ip, dst_ip),
    ) {
        Ok(packet) => packet,
        Err(err) => return, // TODO(joshlf): Do something else here?
    };

    #[ipv4addr]
    {
        match packet {
            Icmpv4Packet::EchoRequest(echo_request) => {
                let req = *echo_request.message();
                let code = echo_request.code();
                // drop packet so we can re-use the underlying buffer
                mem::drop(echo_request);

                increment_counter!(ctx, "receive_icmp_packet::echo_request");

                // we're responding to the sender, so these are flipped
                let (src_ip, dst_ip) = (dst_ip, src_ip);
                // TODO(joshlf): Do something if send_ip_packet returns an
                // error?
                send_ip_packet(ctx, dst_ip, IpProto::Icmp, |src_ip| {
                    BufferSerializer::new_vec(buffer).encapsulate(
                        IcmpPacketBuilder::<Ipv4, &[u8], _>::new(src_ip, dst_ip, code, req.reply()),
                    )
                });
            }
            Icmpv4Packet::EchoReply(echo_reply) => {
                increment_counter!(ctx, "receive_icmp_packet::echo_reply");
                trace!("receive_icmp_packet: Received an EchoReply message");
            }
            _ => log_unimplemented!(
                (),
                "ip::icmp::receive_icmp_packet: Not implemented for this packet type"
            ),
        }
    }

    #[ipv6addr]
    {
        match packet {
            Icmpv6Packet::EchoRequest(echo_request) => {
                let req = *echo_request.message();
                let code = echo_request.code();
                // drop packet so we can re-use the underlying buffer
                mem::drop(echo_request);

                increment_counter!(ctx, "receive_icmp_packet::echo_request");

                // we're responding to the sender, so these are flipped
                let (src_ip, dst_ip) = (dst_ip, src_ip);
                // TODO(joshlf): Do something if send_ip_packet returns an
                // error?
                send_ip_packet(ctx, dst_ip, IpProto::Icmp, |src_ip| {
                    BufferSerializer::new_vec(buffer).encapsulate(
                        IcmpPacketBuilder::<Ipv6, &[u8], _>::new(src_ip, dst_ip, code, req.reply()),
                    )
                });
            }
            Icmpv6Packet::EchoReply(echo_reply) => {
                increment_counter!(ctx, "receive_icmp_packet::echo_reply");
                trace!("receive_icmp_packet: Received an EchoReply message");
            }
            Icmpv6Packet::RouterSolicitation(_)
            | Icmpv6Packet::RouterAdvertisment(_)
            | Icmpv6Packet::NeighborSolicitation(_)
            | Icmpv6Packet::NeighborAdvertisment(_)
            | Icmpv6Packet::Redirect(_) => {
                ndp::receive_ndp_packet(ctx, device, src_ip, dst_ip, packet);
            }
            _ => log_unimplemented!(
                (),
                "ip::icmp::receive_icmp_packet: Not implemented for this packet type"
            ),
        }
    }
}

/// Send an ICMP message in response to receiving a packet with a parameter
/// problem.
///
/// `send_icmp_parameter_problem` sends the appropriate ICMP or ICMPv6
/// message in response to receiving an IP packet from `src_ip` to `dst_ip`
/// with a parameter problem.
///
/// `original_packet` contains the contents of the entire original packet -
/// including all IP headers. `header_len` is the length of either the IPv4
/// header or all IPv6 headers (including extension headers) *before* the
/// payload with the problematic Next Header type. In other words, in an IPv6
/// packet with a single header with a Next Header type of TCP, its `header_len`
/// would be the length of the single header (40 bytes). `code` is the
/// parameter problem code as defined by ICMPv4 (for IPv4 packets), or ICMPv6
/// (for IPv6 packets). `pointer` is the index to the problematic location in
/// the original IP packet.
#[specialize_ip_address]
pub(crate) fn send_icmp_parameter_problem<D: EventDispatcher, A: IpAddress, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    src_ip: A,
    dst_ip: A,
    code: <A::Version as IcmpIpTypes>::ParameterProblemCode,
    pointer: <A::Version as IcmpIpTypes>::ParameterProblemPointer,
    original_packet: B,
    header_len: usize,
) {
    increment_counter!(ctx, "send_icmp_parameter_problem");

    // Check the conditions of RFC 1122 section 3.2.2 in order to determine
    // whether we MUST NOT send an ICMP error message.
    if !should_send_icmp_error(frame_dst, src_ip, dst_ip) {
        return;
    }

    #[ipv4addr]
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

    #[ipv6addr]
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

/// Send an ICMP message in response to receiving a packet destined for an
/// unsupported IPv4 protocol or IPv6 next header.
///
/// `send_icmp_protocol_unreachable` sends the appropriate ICMP or ICMPv6
/// message in response to receiving an IP packet from `src_ip` to `dst_ip`
/// identifying an unsupported protocol. For IPv4, this is an ICMP "destination
/// unreachable" message with a "protocol unreachable" code. For IPv6, this is
/// an ICMPv6 "parameter problem" message with an "unrecognized next header
/// type" code.
///
/// `original_packet` contains the contents of the entire original packet -
/// including all IP headers. `header_len` is the length of either the IPv4
/// header or all IPv6 headers (including extension headers) *before* the
/// payload with the problematic Next Header type. In other words, in an IPv6
/// packet with a single header with a Next Header type of TCP, it `header_len`
/// would be the length of the single header (40 bytes).
#[specialize_ip_address]
pub(crate) fn send_icmp_protocol_unreachable<D: EventDispatcher, A: IpAddress, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    original_packet: B,
    header_len: usize,
) {
    increment_counter!(ctx, "send_icmp_protocol_unreachable");

    // Check the conditions of RFC 1122 section 3.2.2 in order to determine
    // whether we MUST NOT send an ICMP error message. Unlike other
    // send_icmp_xxx functions, we do not check to see whether the inbound
    // packet is an ICMP error message - we already know it's not since its
    // protocol was unsupported.
    if !should_send_icmp_error(frame_dst, src_ip, dst_ip) {
        return;
    }

    #[ipv4addr]
    send_icmpv4_dest_unreachable(
        ctx,
        device,
        src_ip,
        dst_ip,
        Icmpv4DestUnreachableCode::DestProtocolUnreachable,
        original_packet,
        header_len,
    );

    #[ipv6addr]
    send_icmpv6_parameter_problem(
        ctx,
        device,
        src_ip,
        dst_ip,
        Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
        // Per RFC 4443, the pointer refers to the first byte of the
        // packet whose Next Header field was unrecognized. It is
        // measured as an offset from the beginning of the first IPv6
        // header. E.g., a pointer of 40 (the length of a single IPv6
        // header) would indicate that the Next Header field from that
        // header - and hence of the first encapsulated packet - was
        // unrecognized.
        //
        // NOTE: Since header_len is a usize, this could theoretically
        // be a lossy conversion. However, all that means in practice is
        // that, if a remote host somehow managed to get us to process a
        // frame with a 4GB IP header and send an ICMP response, the
        // pointer value would be wrong. It's not worth wasting special
        // logic to avoid generating a malformed packet in a case that
        // will almost certainly never happen.
        Icmpv6ParameterProblem::new(header_len as u32),
        original_packet,
        header_len,
    );
}

/// Send an ICMP message in response to receiving a packet destined for an
/// unreachable local transport-layer port.
///
/// `send_icmp_port_unreachable` sends the appropriate ICMP or ICMPv6 message in
/// response to receiving an IP packet from `src_ip` to `dst_ip` with an
/// unreachable local transport-layer port. For both IPv4 and IPv6, this is an
/// ICMP(v6) "destination unreachable" message with a "port unreachable" code.
///
/// `original_packet` contains the contents of the entire original packet -
/// including all IP headers. `ipv4_header_len` is the length of the IPv4
/// header. It is ignored for IPv6.
#[specialize_ip_address]
pub(crate) fn send_icmp_port_unreachable<D: EventDispatcher, A: IpAddress, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    src_ip: A,
    dst_ip: A,
    original_packet: B,
    ipv4_header_len: usize,
) {
    increment_counter!(ctx, "send_icmp_port_unreachable");

    // Check the conditions of RFC 1122 section 3.2.2 in order to determine
    // whether we MUST NOT send an ICMP error message. Unlike other
    // send_icmp_xxx functions, we do not check to see whether the inbound
    // packet is an ICMP error message - we already know it's not since ICMP is
    // not one of the protocols that can generate "port unreachable" errors.
    if !should_send_icmp_error(frame_dst, src_ip, dst_ip) {
        return;
    }

    #[ipv4addr]
    send_icmpv4_dest_unreachable(
        ctx,
        device,
        src_ip,
        dst_ip,
        Icmpv4DestUnreachableCode::DestPortUnreachable,
        original_packet,
        ipv4_header_len,
    );
    #[ipv6addr]
    send_icmpv6_dest_unreachable(
        ctx,
        device,
        src_ip,
        dst_ip,
        Icmpv6DestUnreachableCode::PortUnreachable,
        original_packet,
    );
}

/// Send an ICMP message in response to receiving a packet destined for an
/// unreachable network.
///
/// `send_icmp_net_unreachable` sends the appropriate ICMP or ICMPv6 message in
/// response to receiving an IP packet from `src_ip` to an unreachable `dst_ip`.
/// For IPv4, this is an ICMP "destination unreachable" message with a "net
/// unreachable" code. For IPv6, this is an ICMPv6 "destination unreachable"
/// message with a "no route to destination" code.
///
/// `original_packet` contains the contents of the entire original packet -
/// including all IP headers. `ipv4_header_len` is the length of the IPv4
/// header. It is ignored for IPv6.
#[specialize_ip_address]
pub(crate) fn send_icmp_net_unreachable<D: EventDispatcher, A: IpAddress, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    original_packet: B,
    header_len: usize,
) {
    increment_counter!(ctx, "send_icmp_net_unreachable");

    // Check the conditions of RFC 1122 section 3.2.2 in order to determine
    // whether we MUST NOT send an ICMP error message. should_send_icmp_error
    // does not handle the "ICMP error message" case, so we check that
    // separately with a call to is_icmp_error_message.
    if !should_send_icmp_error(frame_dst, src_ip, dst_ip)
        || is_icmp_error_message(proto, &original_packet.as_ref()[header_len..])
    {
        return;
    }

    #[ipv4addr]
    send_icmpv4_dest_unreachable(
        ctx,
        device,
        src_ip,
        dst_ip,
        Icmpv4DestUnreachableCode::DestNetworkUnreachable,
        original_packet,
        header_len,
    );

    #[ipv6addr]
    send_icmpv6_dest_unreachable(
        ctx,
        device,
        src_ip,
        dst_ip,
        Icmpv6DestUnreachableCode::NoRoute,
        original_packet,
    );
}

/// Send an ICMP message in response to receiving a packet whose TTL has
/// expired.
///
/// `send_icmp_ttl_expired` sends the appropriate ICMP or ICMPv6 message in
/// response to receiving an IP packet from `src_ip` to `dst_ip` whose TTL
/// (IPv4)/Hop Limit (IPv6) has expired. For IPv4, this is an ICMP "time
/// exceeded" message with a "time to live exceeded in transit" code. For IPv6,
/// this is an ICMPv6 "time exceeded" message with a "hop limit exceeded in
/// transit" code.
///
/// `original_packet` contains the contents of the entire original packet -
/// including all IP headers. `ipv4_header_len` is the length of the IPv4
/// header. It is ignored for IPv6.
#[specialize_ip_address]
pub(crate) fn send_icmp_ttl_expired<D: EventDispatcher, A: IpAddress, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    src_ip: A,
    dst_ip: A,
    proto: IpProto,
    original_packet: B,
    header_len: usize,
) {
    increment_counter!(ctx, "send_icmp_ttl_expired");

    // Check the conditions of RFC 1122 section 3.2.2 in order to determine
    // whether we MUST NOT send an ICMP error message. should_send_icmp_error
    // does not handle the "ICMP error message" case, so we check that
    // separately with a call to is_icmp_error_message.
    if !should_send_icmp_error(frame_dst, src_ip, dst_ip)
        || is_icmp_error_message(proto, &original_packet.as_ref()[header_len..])
    {
        return;
    }

    #[ipv4addr]
    {
        // Per RFC 792, body contains entire IPv4 header + 64 bytes of
        // original body.
        let mut original_packet = original_packet;
        original_packet.shrink_back_to(header_len + 64);
        // TODO(joshlf): Do something if send_icmp_response returns an error?
        send_icmp_response(ctx, device, src_ip, dst_ip, IpProto::Icmp, |local_ip| {
            BufferSerializer::new_vec(original_packet).encapsulate(IcmpPacketBuilder::<
                Ipv4,
                &[u8],
                _,
            >::new(
                local_ip,
                src_ip,
                Icmpv4TimeExceededCode::TtlExpired,
                IcmpTimeExceeded::default(),
            ))
        });
    }
    #[ipv6addr]
    {
        // Per RFC 4443, body contains as much of the original body as
        // possible without exceeding IPv6 minimum MTU.
        let mut original_packet = original_packet;
        original_packet.shrink_back_to(IPV6_MIN_MTU as usize);
        // TODO(joshlf): Do something if send_icmp_response returns an error?
        send_icmp_response(ctx, device, src_ip, dst_ip, IpProto::Icmpv6, |local_ip| {
            BufferSerializer::new_vec(original_packet).encapsulate(IcmpPacketBuilder::<
                Ipv6,
                &[u8],
                _,
            >::new(
                local_ip,
                src_ip,
                Icmpv6TimeExceededCode::HopLimitExceeded,
                IcmpTimeExceeded::default(),
            ))
        });
    }
}

// TODO(joshlf): Test send_icmpv6_packet_too_big once we support dummy IPv6 test
// setups.

/// Send an ICMPv6 message in response to receiving a packet whose size exceeds
/// the MTU of the next hop interface.
///
/// `send_icmpv6_packet_too_big` sends an ICMPv6 "packet too big" message in
/// response to receiving an IP packet from `src_ip` to `dst_ip` whose size
/// exceeds the `mtu` of the next hop interface.
pub(crate) fn send_icmpv6_packet_too_big<D: EventDispatcher, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    proto: IpProto,
    mtu: u32,
    mut original_packet: B,
    header_len: usize,
) {
    increment_counter!(ctx, "send_icmpv6_packet_too_big");

    // Check the conditions of RFC 1122 section 3.2.2 in order to determine
    // whether we MUST NOT send an ICMP error message. should_send_icmp_error
    // does not handle the "ICMP error message" case, so we check that
    // separately with a call to is_icmp_error_message.
    if !should_send_icmp_error(frame_dst, src_ip, dst_ip)
        || is_icmp_error_message(proto, &original_packet.as_ref()[header_len..])
    {
        return;
    }

    // Per RFC 4443, body contains as much of the original body as possible
    // without exceeding IPv6 minimum MTU.
    original_packet.shrink_back_to(IPV6_MIN_MTU as usize);
    send_icmp_response(ctx, device, src_ip, dst_ip, IpProto::Icmpv6, |local_ip| {
        BufferSerializer::new_vec(original_packet).encapsulate(
            IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                local_ip,
                src_ip,
                IcmpUnusedCode,
                Icmpv6PacketTooBig::new(mtu),
            ),
        )
    });
}

fn send_icmpv4_parameter_problem<D: EventDispatcher, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    code: Icmpv4ParameterProblemCode,
    parameter_problem: Icmpv4ParameterProblem,
    original_packet: B,
    header_len: usize,
) {
    increment_counter!(ctx, "send_icmpv4_parameter_problem");

    // Per RFC 792, body contains entire IPv4 header + 64 bytes of original
    // body.
    let mut original_packet = original_packet;
    original_packet.shrink_back_to(header_len + 64);
    // TODO(joshlf): Do something if send_icmp_response returns an error?
    send_icmp_response(ctx, device, src_ip, dst_ip, IpProto::Icmp, |local_ip| {
        BufferSerializer::new_vec(original_packet).encapsulate(
            IcmpPacketBuilder::<Ipv4, &[u8], _>::new(local_ip, src_ip, code, parameter_problem),
        )
    });
}

fn send_icmpv6_parameter_problem<D: EventDispatcher, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    code: Icmpv6ParameterProblemCode,
    parameter_problem: Icmpv6ParameterProblem,
    original_packet: B,
    header_len: usize,
) {
    increment_counter!(ctx, "send_icmpv6_parameter_problem");

    // Per RFC 4443, body contains as much of the original body as
    // possible without exceeding IPv6 minimum MTU.
    let mut original_packet = original_packet;
    original_packet.shrink_back_to(IPV6_MIN_MTU as usize);
    // TODO(joshlf): Do something if send_icmp_response returns an error?
    send_icmp_response(ctx, device, src_ip, dst_ip, IpProto::Icmpv6, |local_ip| {
        BufferSerializer::new_vec(original_packet).encapsulate(
            IcmpPacketBuilder::<Ipv6, &[u8], _>::new(local_ip, src_ip, code, parameter_problem),
        )
    });
}

fn send_icmpv4_dest_unreachable<D: EventDispatcher, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    code: Icmpv4DestUnreachableCode,
    original_packet: B,
    header_len: usize,
) {
    // Per RFC 792, body contains entire IPv4 header + 64 bytes of original
    // body.
    let mut original_packet = original_packet;
    original_packet.shrink_back_to(header_len + 64);
    // TODO(joshlf): Do something if send_icmp_response returns an error?
    send_icmp_response(ctx, device, src_ip, dst_ip, IpProto::Icmp, |local_ip| {
        BufferSerializer::new_vec(original_packet).encapsulate(
            IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                local_ip,
                src_ip,
                code,
                IcmpDestUnreachable::default(),
            ),
        )
    });
}

fn send_icmpv6_dest_unreachable<D: EventDispatcher, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    code: Icmpv6DestUnreachableCode,
    original_packet: B,
) {
    // Per RFC 4443, body contains as much of the original body as possible
    // without exceeding IPv6 minimum MTU.
    let mut original_packet = original_packet;
    original_packet.shrink_back_to(IPV6_MIN_MTU as usize);
    // TODO(joshlf): Do something if send_icmp_response returns an error?
    send_icmp_response(ctx, device, src_ip, dst_ip, IpProto::Icmpv6, |local_ip| {
        BufferSerializer::new_vec(original_packet).encapsulate(
            IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                local_ip,
                src_ip,
                code,
                IcmpDestUnreachable::default(),
            ),
        )
    });
}

/// Should we send an ICMP response?
///
/// `should_send_icmp_error` implements the logic described in RFC 1122 section
/// 3.2.2. It decides whether, upon receiving an incoming packet with the given
/// parameters, we should send an ICMP response or not. In particular, we do not
/// send an ICMP response if we've received:
/// - a packet destined to a broadcast or multicast address
/// - a packet sent in a link-layer broadcast
/// - a non-initial fragment
/// - a packet whose source address does not define a single host (a
///   zero/unspecified address, a loopback address, a broadcast address, a
///   multicast address, or a Class E address)
///
/// Note that `should_send_icmp_error` does NOT check whether the incoming
/// packet contained an ICMP error message. This is because that check is
/// unnecessary for some ICMP error conditions. The ICMP error message check can
/// be performed separately with `is_icmp_error_message`.
fn should_send_icmp_error<A: IpAddress>(frame_dst: FrameDestination, src_ip: A, dst_ip: A) -> bool {
    // TODO(joshlf): Implement the rest of the rules:
    // - a packet destined to a subnet broadcast address
    // - a non-initial fragment
    // - a packet whose source address is a subnet broadcast address
    !(dst_ip.is_multicast()
        || dst_ip.with_v4(Ipv4Addr::is_global_broadcast, false)
        || frame_dst.is_broadcast()
        || src_ip.is_unspecified()
        || src_ip.is_loopback()
        || src_ip.with_v4(Ipv4Addr::is_global_broadcast, false)
        || src_ip.is_multicast()
        || src_ip.with_v4(Ipv4Addr::is_class_e, false))
}

/// Determine whether or not an IP packet body contains an ICMP error message
/// for the purposes of determining whether or not to send an ICMP response.
///
/// `is_icmp_error_message` determines whether `proto` is ICMP or ICMPv6, and if
/// so, attempts to parse `buf` as an ICMP packet in order to determine whether
/// it is an error message or not. If parsing fails, it conservatively assumes
/// that it is an error packet in order to avoid violating RFC 1122 section
/// 3.2.2's MUST NOT directive.
fn is_icmp_error_message(proto: IpProto, buf: &[u8]) -> bool {
    match proto {
        IpProto::Icmp => {
            peek_message_type::<Icmpv4MessageType>(buf).map(IcmpMessageType::is_err).unwrap_or(true)
        }
        IpProto::Icmpv6 => {
            peek_message_type::<Icmpv6MessageType>(buf).map(IcmpMessageType::is_err).unwrap_or(true)
        }
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use packet::{Buf, BufferSerializer, Serializer};

    use std::fmt::Debug;
    #[cfg(feature = "udp-icmp-port-unreachable")]
    use std::num::NonZeroU16;

    use super::*;
    use crate::device::{DeviceId, FrameDestination};
    use crate::ip::{receive_ip_packet, IpExt, Ipv4, Ipv4Addr};
    use crate::testutil::{DummyEventDispatcher, DummyEventDispatcherBuilder, DUMMY_CONFIG_V4};
    use crate::wire::icmp::{
        IcmpEchoRequest, IcmpMessage, IcmpPacket, IcmpUnusedCode, MessageBody,
    };
    #[cfg(feature = "udp-icmp-port-unreachable")]
    use crate::wire::udp::UdpPacketBuilder;

    /// Test that receiving a particular IP packet results in a particular ICMP
    /// response.
    ///
    /// Test that receiving an IP packet from remote host
    /// `DUMMY_CONFIG.remote_ip` to host `dst_ip` with `ttl` and `proto` results
    /// in all of the counters in `assert_counters` being triggered at least
    /// once, and exactly one ICMP packet being sent in response with
    /// `expect_message` and `expect_code`.
    ///
    /// The state is initialized to `testutil::DUMMY_CONFIG` when testing.
    #[allow(clippy::too_many_arguments)]
    fn test_receive_ip_packet<
        C: PartialEq + Debug,
        M: for<'a> IcmpMessage<Ipv4, &'a [u8], Code = C> + PartialEq + Debug,
        F: for<'a> Fn(&IcmpPacket<Ipv4, &'a [u8], M>),
    >(
        body: &mut [u8],
        dst_ip: Ipv4Addr,
        ttl: u8,
        proto: IpProto,
        assert_counters: &[&str],
        expect_message: M,
        expect_code: C,
        f: F,
    ) {
        crate::testutil::set_logger_for_test();
        let buffer = BufferSerializer::new_vec(Buf::new(body, ..))
            .encapsulate(<Ipv4 as IpExt<&[u8]>>::PacketBuilder::new(
                DUMMY_CONFIG_V4.remote_ip,
                dst_ip,
                ttl,
                proto,
            ))
            .serialize_outer()
            .unwrap();

        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V4)
            .build::<DummyEventDispatcher>();
        // currently only used by test_ttl_exceeded
        ctx.state_mut().ip.v4.forward = true;
        receive_ip_packet::<_, _, Ipv4>(
            &mut ctx,
            DeviceId::new_ethernet(1),
            FrameDestination::Unicast,
            buffer,
        );

        for counter in assert_counters {
            assert!(*ctx.state().test_counters.get(counter) > 0, "counter at zero: {}", counter);
        }

        assert_eq!(ctx.dispatcher().frames_sent().len(), 1);
        let (src_mac, dst_mac, src_ip, dst_ip, message, code) =
            crate::testutil::parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv4, _, M, _>(
                &ctx.dispatcher().frames_sent()[0].1,
                f,
            )
            .unwrap();

        assert_eq!(src_mac, DUMMY_CONFIG_V4.local_mac);
        assert_eq!(dst_mac, DUMMY_CONFIG_V4.remote_mac);
        assert_eq!(src_ip, DUMMY_CONFIG_V4.local_ip);
        assert_eq!(dst_ip, DUMMY_CONFIG_V4.remote_ip);
        assert_eq!(message, expect_message);
        assert_eq!(code, expect_code);
    }

    #[test]
    fn test_receive_echo() {
        crate::testutil::set_logger_for_test();

        let req = IcmpEchoRequest::new(0, 0);
        let req_body = &[1, 2, 3, 4];
        let mut buffer = BufferSerializer::new_vec(Buf::new(req_body.to_vec(), ..))
            .encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                DUMMY_CONFIG_V4.remote_ip,
                DUMMY_CONFIG_V4.local_ip,
                IcmpUnusedCode,
                req,
            ))
            .serialize_outer()
            .unwrap();
        test_receive_ip_packet(
            buffer.as_mut(),
            DUMMY_CONFIG_V4.local_ip,
            64,
            IpProto::Icmp,
            &["receive_icmp_packet::echo_request", "send_ip_packet"],
            req.reply(),
            IcmpUnusedCode,
            |packet| assert_eq!(packet.original_packet().bytes(), req_body),
        );
    }

    #[test]
    fn test_protocol_unreachable() {
        // Receive an IP packet for an unreachable protocol (255). Check to make
        // sure that we respond with the appropriate ICMP message.
        //
        // TODO(joshlf): Also perform the check for IPv6 once we support dummy
        // IPv6 networks.
        test_receive_ip_packet(
            &mut [0u8; 128],
            DUMMY_CONFIG_V4.local_ip,
            64,
            IpProto::Other(255),
            &["send_icmp_protocol_unreachable", "send_icmp_response"],
            IcmpDestUnreachable::default(),
            Icmpv4DestUnreachableCode::DestProtocolUnreachable,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }

    #[test]
    #[cfg(feature = "udp-icmp-port-unreachable")]
    fn test_port_unreachable() {
        // TODO(joshlf): Use TCP in addition to UDP since UDP only works with
        // the udp-icmp-port-unreachable feature enabled.

        // Receive an IP packet for an unreachable UDP port (1234). Check to
        // make sure that we respond with the appropriate ICMP message.
        //
        // TODO(joshlf):
        // - Also perform the check for IPv6 once we support dummy IPv6
        //   networks.
        // - Perform the same check for TCP once the logic is implemented
        let mut buf = [0u8; 128];
        let mut buffer = BufferSerializer::new_vec(Buf::new(&mut buf[..], ..))
            .encapsulate(UdpPacketBuilder::new(
                DUMMY_CONFIG_V4.remote_ip,
                DUMMY_CONFIG_V4.local_ip,
                None,
                NonZeroU16::new(1234).unwrap(),
            ))
            .serialize_outer();
        test_receive_ip_packet(
            buffer.as_mut(),
            DUMMY_CONFIG_V4.local_ip,
            64,
            IpProto::Udp,
            &["send_icmp_port_unreachable", "send_icmp_response"],
            IcmpDestUnreachable::default(),
            Icmpv4DestUnreachableCode::DestPortUnreachable,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }

    #[test]
    fn test_net_unreachable() {
        // Receive an IP packet for an unreachable destination address. Check to
        // make sure that we respond with the appropriate ICMP message.
        //
        // TODO(joshlf): Also perform the check for IPv6 once we support dummy
        // IPv6 networks.
        test_receive_ip_packet(
            &mut [0u8; 128],
            Ipv4Addr::new([1, 2, 3, 4]),
            64,
            IpProto::Udp,
            &["send_icmp_net_unreachable", "send_icmp_response"],
            IcmpDestUnreachable::default(),
            Icmpv4DestUnreachableCode::DestNetworkUnreachable,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }

    #[test]
    fn test_ttl_expired() {
        // Receive an IP packet with an expired TTL. Check to make sure that we
        // respond with the appropriate ICMP message.
        //
        // TODO(joshlf): Also perform the check for IPv6 once we support dummy
        // IPv6 networks.
        test_receive_ip_packet(
            &mut [0u8; 128],
            DUMMY_CONFIG_V4.remote_ip,
            1,
            IpProto::Udp,
            &["send_icmp_ttl_expired", "send_icmp_response"],
            IcmpTimeExceeded::default(),
            Icmpv4TimeExceededCode::TtlExpired,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }
}
