// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Control Message Protocol (ICMP).

use std::hash::Hash;
use std::mem;

use byteorder::{ByteOrder, NetworkEndian};
use log::trace;
use packet::{BufferMut, BufferSerializer, Serializer, TruncateDirection};
use specialize_ip_macro::{specialize_ip, specialize_ip_address};

use crate::device::{ndp, DeviceId, FrameDestination};
use crate::error;
use crate::ip::path_mtu::{update_pmtu_if_less, update_pmtu_next_lower};
use crate::ip::{
    send_icmp_response, send_ip_packet, Ip, IpAddress, IpProto, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr,
    IPV6_MIN_MTU,
};
use crate::transport::ConnAddrMap;
use crate::types::MulticastAddress;
use crate::wire::icmp::{
    peek_message_type, IcmpDestUnreachable, IcmpEchoReply, IcmpEchoRequest, IcmpIpExt, IcmpMessage,
    IcmpMessageType, IcmpPacket, IcmpPacketBuilder, IcmpParseArgs, IcmpTimeExceeded,
    IcmpUnusedCode, Icmpv4DestUnreachableCode, Icmpv4MessageType, Icmpv4Packet,
    Icmpv4ParameterProblem, Icmpv4ParameterProblemCode, Icmpv4TimeExceededCode,
    Icmpv6DestUnreachableCode, Icmpv6MessageType, Icmpv6Packet, Icmpv6PacketTooBig,
    Icmpv6ParameterProblem, Icmpv6ParameterProblemCode, Icmpv6TimeExceededCode, MessageBody,
};
use crate::{Context, EventDispatcher, StackState};
use zerocopy::ByteSlice;

/// A builder for ICMP state.
pub struct IcmpStateBuilder {
    icmpv4_send_timestamp_reply: bool,
}

impl Default for IcmpStateBuilder {
    fn default() -> IcmpStateBuilder {
        IcmpStateBuilder { icmpv4_send_timestamp_reply: false }
    }
}

impl IcmpStateBuilder {
    /// Enable or disable replying to ICMPv4 Timestamp Request messages with
    /// Timestamp Reply messages (default: disabled).
    ///
    /// Enabling this can introduce a very minor vulnerability in which an
    /// attacker can learn the system clock's time, which in turn can aid in
    /// attacks against time-based authentication systems.
    pub fn icmpv4_send_timestamp_reply(&mut self, send_timestamp_reply: bool) -> &mut Self {
        self.icmpv4_send_timestamp_reply = send_timestamp_reply;
        self
    }

    pub(crate) fn build<D: EventDispatcher>(self) -> IcmpState<D> {
        IcmpState {
            v4: Icmpv4State {
                conns: ConnAddrMap::default(),
                send_timestamp_reply: self.icmpv4_send_timestamp_reply,
            },
            v6: Icmpv6State { conns: ConnAddrMap::default() },
        }
    }
}

/// The state associated with the ICMP layer.
pub(crate) struct IcmpState<D: EventDispatcher> {
    v4: Icmpv4State<D>,
    v6: Icmpv6State<D>,
}

struct Icmpv4State<D: EventDispatcher> {
    conns: ConnAddrMap<D::IcmpConn, IcmpAddr<Ipv4Addr>>,
    send_timestamp_reply: bool,
}

struct Icmpv6State<D: EventDispatcher> {
    conns: ConnAddrMap<D::IcmpConn, IcmpAddr<Ipv6Addr>>,
}

#[derive(Copy, Clone, Eq, PartialEq, Hash)]
struct IcmpAddr<A: IpAddress> {
    // TODO(brunodalbo) for now, ICMP connections are only bound to a remote
    //  address and an icmp_id. This will be improved with the addition of
    //  sockets_v2
    remote_addr: A,
    icmp_id: u16,
}

/// An event dispatcher for the ICMP layer.
///
/// See the `EventDispatcher` trait in the crate root for more details.
pub trait IcmpEventDispatcher {
    /// A key identifying an ICMP connection.
    ///
    /// An `IcmpConn` is an opaque identifier which uniquely identifies a
    /// particular ICMP connection. When registering a new connection, a new
    /// `IcmpConn` must be provided. When the stack invokes methods on this
    /// trait related to a connection, the corresponding `IcmpConn` will be
    /// provided.
    ///
    /// ICMP connections are disambiguated using the ICMP "ID" field. When a new
    /// connection is registered, a new ID is allocated to that connection.
    type IcmpConn: Clone + Eq + Hash;

    /// Receive an ICMP echo reply.
    fn receive_icmp_echo_reply(&mut self, conn: &Self::IcmpConn, seq_num: u16, data: &[u8]) {
        log_unimplemented!((), "IcmpEventDispatcher::receive_icmp_echo_reply: not implemented");
    }
}

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
                receive_icmp_echo_reply(ctx, src_ip, dst_ip, echo_reply);
            }
            Icmpv4Packet::TimestampRequest(_timestamp_request) => {
                if ctx.state().ip.icmp.v4.send_timestamp_reply {
                    log_unimplemented!((), "ip::icmp::receive_icmp_packet: Not implemented for sending Timestamp Reply");
                } else {
                    trace!("receive_icmp_packet: Silently ignoring Timestamp message");
                }
            }
            Icmpv4Packet::DestUnreachable(dest_unreachable) => {
                increment_counter!(ctx, "receive_icmp_packet::dest_unreachable");
                trace!("receive_icmp_packet: Received a Destination Unreachable message");

                if dest_unreachable.code() == Icmpv4DestUnreachableCode::FragmentationRequired {
                    let next_hop_mtu = dest_unreachable.message().next_hop_mtu();

                    if let Some(next_hop_mtu) = dest_unreachable.message().next_hop_mtu() {
                        // We are updating the path MTU from the destination address of this
                        // `packet` (which is an IP address on this node) to some remote
                        // (identified by the source address of this `packet`).
                        //
                        // `update_pmtu_if_less` may return an error, but it will only happen if
                        // the Dest Unreachable message's mtu field had a value that was less than
                        // the IPv4 minimum mtu (which as per IPv4 RFC 791, must not happen).
                        update_pmtu_if_less(ctx, dst_ip, src_ip, u32::from(next_hop_mtu.get()));
                    } else {
                        // If the Next-Hop MTU from an incoming ICMP message is `0`, then we assume
                        // the source node of the ICMP message does not implement RFC 1191 and
                        // therefore does not actually use the Next-Hop MTU field and still
                        // considers it as an unused field.
                        //
                        // In this case, the only information we have is the size of the
                        // original IP packet that was too big (the original packet header
                        // should be included in the ICMP response). Here we will simply
                        // reduce our PMTU estimate to a value less than the total length
                        // of the original packet. See RFC 1191 section 5.
                        //
                        // `update_pmtu_next_lower` may return an error, but it will only happen if
                        // no valid lower value exists from the original packet's length. It is safe
                        // to silently ignore the error when we have no valid lower PMTU value as
                        // the node from `src_ip` would not be IP RFC compliant and we expect this
                        // to be very rare (for IPv4, the lowest MTU value for a link can be 68
                        // bytes).
                        let original_packet_buf = dest_unreachable.body().bytes();
                        if original_packet_buf.len() >= 4 {
                            // We need the first 4 bytes as the total length field is at bytes 2/3
                            // of the original packet buffer.
                            let total_len = NetworkEndian::read_u16(&original_packet_buf[2..4]);

                            trace!("receive_icmp_packet: Next-Hop MTU is 0 so using the next best PMTU value from {}", total_len);

                            update_pmtu_next_lower(ctx, dst_ip, src_ip, u32::from(total_len));
                        } else {
                            // Ok to silently ignore as RFC 792 requires nodes to send the original
                            // IP packet header + 64 bytes of the original IP packet's body so the
                            // node itself is already violating the RFC.
                            trace!("receive_icmp_packet: Original packet buf is too small to get original packet len so ignoring");
                        }
                    }
                } else {
                    log_unimplemented!((), "ip::icmp::receive_icmp_packet: Not implemented for this ICMP destination unreachable code {:?}", dest_unreachable.code());
                }
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
                send_ip_packet(ctx, dst_ip, IpProto::Icmpv6, |src_ip| {
                    BufferSerializer::new_vec(buffer).encapsulate(
                        IcmpPacketBuilder::<Ipv6, &[u8], _>::new(src_ip, dst_ip, code, req.reply()),
                    )
                });
            }
            Icmpv6Packet::EchoReply(echo_reply) => {
                increment_counter!(ctx, "receive_icmp_packet::echo_reply");
                trace!("receive_icmp_packet: Received an EchoReply message");
                receive_icmp_echo_reply(ctx, src_ip, dst_ip, echo_reply);
            }
            Icmpv6Packet::RouterSolicitation(_)
            | Icmpv6Packet::RouterAdvertisment(_)
            | Icmpv6Packet::NeighborSolicitation(_)
            | Icmpv6Packet::NeighborAdvertisment(_)
            | Icmpv6Packet::Redirect(_) => {
                ndp::receive_ndp_packet(ctx, device, src_ip, dst_ip, packet);
            }
            Icmpv6Packet::PacketTooBig(packet_too_big) => {
                increment_counter!(ctx, "receive_icmp_packet::packet_too_big");
                trace!("receive_icmp_packet: Received a Packet Too Big message");
                // We are updating the path MTU from the destination address of this
                // `packet` (which is an IP address on this node) to some remote
                // (identified by the source address of this `packet`).
                //
                // `update_pmtu_if_less` may return an error, but it will only happen if
                // the Packet Too Big message's mtu field had a value that was less than
                // the IPv6 minimum mtu (which as per IPv6 RFC 8200, must not happen).
                update_pmtu_if_less(ctx, dst_ip, src_ip, packet_too_big.message().mtu());
            }
            _ => log_unimplemented!(
                (),
                "ip::icmp::receive_icmp_packet: Not implemented for this packet type"
            ),
        }
    }
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

    // Check whether we MUST NOT send an ICMP error message. Unlike other
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

    // Check whether we MUST NOT send an ICMP error message. Unlike other
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

    // Check whether we MUST NOT send an ICMP error message. should_send_icmp_error
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
    mut original_packet: B,
    header_len: usize,
) {
    increment_counter!(ctx, "send_icmp_ttl_expired");

    // Check whether we MUST NOT send an ICMP error message. should_send_icmp_error
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
        original_packet.shrink_back_to(header_len + 64);
        // TODO(joshlf): Do something if send_icmp_response returns an error?
        send_icmp_response(
            ctx,
            device,
            src_ip,
            dst_ip,
            IpProto::Icmp,
            |local_ip| {
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
            },
            None,
        );
    }
    #[ipv6addr]
    {
        // TODO(joshlf): Do something if send_icmp_response returns an error?
        send_icmp_response(
            ctx,
            device,
            src_ip,
            dst_ip,
            IpProto::Icmpv6,
            |local_ip| {
                let icmp_builder = IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                    local_ip,
                    src_ip,
                    Icmpv6TimeExceededCode::HopLimitExceeded,
                    IcmpTimeExceeded::default(),
                );

                // Per RFC 4443, body contains as much of the original body as
                // possible without exceeding IPv6 minimum MTU.
                BufferSerializer::new_vec_truncate(original_packet, TruncateDirection::DiscardBack)
                    .encapsulate(icmp_builder)
            },
            Some(IPV6_MIN_MTU),
        );
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
    original_packet: B,
    header_len: usize,
) {
    increment_counter!(ctx, "send_icmpv6_packet_too_big");

    // Check whether we MUST NOT send an ICMP error message. should_send_icmp_error
    // does not handle the "ICMP error message" case, so we check that
    // separately with a call to is_icmp_error_message.
    //
    // Note, here we explicitly let `should_send_icmpv6_error` allow a multicast
    // destination (link-layer or destination ip) as RFC 4443 section 2.4.e
    // explicitly allows sending an ICMP response if the original packet was
    // sent to a multicast IP or link layer if the ICMP response message will be
    // a Packet Too Big Message.
    if !should_send_icmpv6_error(frame_dst, src_ip, dst_ip, true)
        || is_icmp_error_message(proto, &original_packet.as_ref()[header_len..])
    {
        return;
    }

    send_icmp_response(
        ctx,
        device,
        src_ip,
        dst_ip,
        IpProto::Icmpv6,
        |local_ip| {
            let icmp_builder = IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                local_ip,
                src_ip,
                IcmpUnusedCode,
                Icmpv6PacketTooBig::new(mtu),
            );

            // Per RFC 4443, body contains as much of the original body as possible
            // without exceeding IPv6 minimum MTU.
            //
            // The final IP packet must fit within the MTU, so we shrink the original packet to
            // the MTU minus the IPv6 and ICMP header sizes.
            BufferSerializer::new_vec_truncate(original_packet, TruncateDirection::DiscardBack)
                .encapsulate(icmp_builder)
        },
        Some(IPV6_MIN_MTU),
    );
}

pub(crate) fn send_icmpv4_parameter_problem<D: EventDispatcher, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    code: Icmpv4ParameterProblemCode,
    parameter_problem: Icmpv4ParameterProblem,
    mut original_packet: B,
    header_len: usize,
) {
    increment_counter!(ctx, "send_icmpv4_parameter_problem");

    // Per RFC 792, body contains entire IPv4 header + 64 bytes of original
    // body.
    original_packet.shrink_back_to(header_len + 64);
    // TODO(joshlf): Do something if send_icmp_response returns an error?
    send_icmp_response(
        ctx,
        device,
        src_ip,
        dst_ip,
        IpProto::Icmp,
        |local_ip| {
            BufferSerializer::new_vec(original_packet).encapsulate(IcmpPacketBuilder::<
                Ipv4,
                &[u8],
                _,
            >::new(
                local_ip,
                src_ip,
                code,
                parameter_problem,
            ))
        },
        None,
    );
}

pub(crate) fn send_icmpv6_parameter_problem<D: EventDispatcher, B: BufferMut>(
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

    // TODO(joshlf): Do something if send_icmp_response returns an error?
    send_icmp_response(
        ctx,
        device,
        src_ip,
        dst_ip,
        IpProto::Icmpv6,
        |local_ip| {
            let icmp_builder =
                IcmpPacketBuilder::<Ipv6, &[u8], _>::new(local_ip, src_ip, code, parameter_problem);

            // Per RFC 4443, body contains as much of the original body as
            // possible without exceeding IPv6 minimum MTU.
            BufferSerializer::new_vec_truncate(original_packet, TruncateDirection::DiscardBack)
                .encapsulate(icmp_builder)
        },
        Some(IPV6_MIN_MTU),
    );
}

fn send_icmpv4_dest_unreachable<D: EventDispatcher, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    code: Icmpv4DestUnreachableCode,
    mut original_packet: B,
    header_len: usize,
) {
    increment_counter!(ctx, "send_icmpv4_dest_unreachable");

    // Per RFC 792, body contains entire IPv4 header + 64 bytes of original
    // body.
    original_packet.shrink_back_to(header_len + 64);
    // TODO(joshlf): Do something if send_icmp_response returns an error?
    send_icmp_response(
        ctx,
        device,
        src_ip,
        dst_ip,
        IpProto::Icmp,
        |local_ip| {
            BufferSerializer::new_vec(original_packet).encapsulate(IcmpPacketBuilder::<
                Ipv4,
                &[u8],
                _,
            >::new(
                local_ip,
                src_ip,
                code,
                IcmpDestUnreachable::default(),
            ))
        },
        None,
    );
}

fn send_icmpv6_dest_unreachable<D: EventDispatcher, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    code: Icmpv6DestUnreachableCode,
    original_packet: B,
) {
    // TODO(joshlf): Do something if send_icmp_response returns an error?
    send_icmp_response(
        ctx,
        device,
        src_ip,
        dst_ip,
        IpProto::Icmpv6,
        |local_ip| {
            let icmp_builder = IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                local_ip,
                src_ip,
                code,
                IcmpDestUnreachable::default(),
            );

            // Per RFC 4443, body contains as much of the original body as
            // possible without exceeding IPv6 minimum MTU.
            BufferSerializer::new_vec_truncate(original_packet, TruncateDirection::DiscardBack)
                .encapsulate(icmp_builder)
        },
        Some(IPV6_MIN_MTU),
    );
}

/// Should we send an ICMP response?
#[specialize_ip_address]
pub(crate) fn should_send_icmp_error<A: IpAddress>(
    frame_dst: FrameDestination,
    src_ip: A,
    dst_ip: A,
) -> bool {
    #[ipv4addr]
    let ret = should_send_icmpv4_error(frame_dst, src_ip, dst_ip);

    // When checking with ICMPv6 rules, do not allow exceptions if the destination
    // is a multicast (outlined by RFC 4443 section 2.4.e).
    //
    // `should_send_icmp_error` is used for the general case where there will be
    // no exceptions. When exceptions are needed, `should_send_icmpv6_error` should
    // be called directly without calling `should_send_icmp_error`
    // (i.e. `send_icmpv6_packet_too_big`).
    #[ipv6addr]
    let ret = should_send_icmpv6_error(frame_dst, src_ip, dst_ip, false);

    ret
}

/// Should we send an ICMP(v4) response?
///
/// `should_send_icmpv4_error` implements the logic described in RFC 1122 section
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
/// Note that `should_send_icmpv4_error` does NOT check whether the incoming
/// packet contained an ICMP error message. This is because that check is
/// unnecessary for some ICMP error conditions. The ICMP error message check can
/// be performed separately with `is_icmp_error_message`.
pub(crate) fn should_send_icmpv4_error(
    frame_dst: FrameDestination,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
) -> bool {
    // TODO(joshlf): Implement the rest of the rules:
    // - a packet destined to a subnet broadcast address
    // - a non-initial fragment
    // - a packet whose source address is a subnet broadcast address
    !(dst_ip.is_multicast()
        || dst_ip.is_global_broadcast()
        || frame_dst.is_broadcast()
        || src_ip.is_unspecified()
        || src_ip.is_loopback()
        || src_ip.is_global_broadcast()
        || src_ip.is_multicast()
        || src_ip.is_class_e())
}

/// Should we send an ICMPv6 response?
///
/// `should_send_icmpv6_error` implements the logic described in RFC 4443 section
/// 2.4.e. It decides whether, upon receiving an incoming packet with the given
/// parameters, we should send an ICMP response or not. In particular, we do not
/// send an ICMP response if we've received:
/// - a packet destined to a multicast address
///   - Two exceptions to this rules:
///     1) the Packet Too Big Message to allow Path MTU discovery to work for IPv6
///        multicast
///     2) the Parameter Problem Message, Code 2 reporting an unrecognized IPv6
///        option that has the Option Type highest-order two bits set to 10
/// - a packet sent as a link-layer multicast or broadcast
///   - same exceptions apply here as well.
/// - a packet whose source address does not define a single host (a
///   zero/unspecified address, a loopback address, or a multicast address)
///
/// If an ICMP response will be a Packet Too Big Message or a Parameter Problem
/// Message, Code 2 reporting an unrecognized IPv6 option that has the Option Type
/// highest-order two bits set to 10, `allow_dst_multicast` must be set to `true`
/// so this function will allow the exception mentioned above.
///
/// Note that `should_send_icmpv6_error` does NOT check whether the incoming
/// packet contained an ICMP error message. This is because that check is
/// unnecessary for some ICMP error conditions. The ICMP error message check can
/// be performed separately with `is_icmp_error_message`.
pub(crate) fn should_send_icmpv6_error(
    frame_dst: FrameDestination,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    allow_dst_multicast: bool,
) -> bool {
    !((!allow_dst_multicast && (dst_ip.is_multicast() || frame_dst.is_broadcast()))
        || src_ip.is_unspecified()
        || src_ip.is_loopback()
        || src_ip.is_multicast())
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

/// Common logic for receiving an ICMP echo reply.
fn receive_icmp_echo_reply<D: EventDispatcher, I: Ip, B: ByteSlice>(
    ctx: &mut Context<D>,
    src_ip: I::Addr,
    dst_ip: I::Addr,
    msg: IcmpPacket<I, B, IcmpEchoReply>,
) where
    IcmpEchoReply: IcmpMessage<I, B>,
{
    let (state, dispatcher) = ctx.state_and_dispatcher();
    // NOTE(brunodalbo): Neither the ICMPv4 or ICMPv6 RFCs explicitly state
    //  what to do in case we receive an "unsolicited" echo reply.
    //  We only expose the replies if we have a registered connection for
    //  the IcmpAddr of the incoming reply for now. Given that a reply should
    //  only be sent in response to a request, an ICMP unreachable-type message
    //  is probably not appropriate for unsolicited replies.
    if let Some(conn) = get_conns::<_, I::Addr>(state)
        .get_by_addr(&IcmpAddr { remote_addr: src_ip, icmp_id: msg.message().id() })
    {
        dispatcher.receive_icmp_echo_reply(conn, msg.message().seq(), msg.body().bytes());
    }
}

/// Send an ICMP echo request on an existing connection.
///
/// # Panics
///
/// `send_icmp_echo_request` panics if `conn` is not associated with a
/// connection for this IP version.
#[specialize_ip]
pub fn send_icmp_echo_request<D: EventDispatcher, I: Ip>(
    ctx: &mut Context<D>,
    conn: &D::IcmpConn,
    seq_num: u16,
    body: &[u8],
) {
    let conns = get_conns::<_, I::Addr>(ctx.state_mut());
    let IcmpAddr { remote_addr, icmp_id } =
        conns.get_by_conn(conn).expect("icmp::send_icmp_echo_request: no such conn").clone();

    let req = IcmpEchoRequest::new(icmp_id, seq_num);

    #[ipv4]
    let proto = IpProto::Icmp;
    #[ipv6]
    let proto = IpProto::Icmpv6;

    // TODO(brunodalbo) for now, ICMP connections are only bound to remote
    //  addresses, which allow us to just send the IP packet with whatever
    //  src ip we resolve the route to. With sockets v2, IcmpAddr will be bound
    //  to a local address and sending this request out will be done
    //  differently.
    crate::ip::send_ip_packet(ctx, remote_addr, proto, |a| {
        body.encapsulate(IcmpPacketBuilder::<I, &[u8], _>::new(a, remote_addr, IcmpUnusedCode, req))
    });
}

/// Creates a new ICMP connection.
///
/// Creates a new ICMP connection with the provided parameters `local_addr`,
/// `remote_addr` and `icmp_id`. The `conn` identifier can be used to index
/// the created connection if it succeeds.
///
/// If a connection with the conflicting parameters already exists, the call
/// fails and returns an [`error::NetstackError`].
pub fn new_icmp_connection<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    conn: D::IcmpConn,
    local_addr: A,
    remote_addr: A,
    icmp_id: u16,
) -> Result<(), error::NetstackError> {
    let conns = get_conns::<_, A>(ctx.state_mut());
    // TODO(brunodalbo) icmp connections are currently only bound to the remote
    //  address. Sockets api v2 will improve this.
    let addr = IcmpAddr { remote_addr, icmp_id };
    if conns.get_by_addr(&addr).is_some() {
        return Err(error::NetstackError::Exists);
    }
    conns.insert(conn, addr);
    Ok(())
}

#[specialize_ip_address]
fn get_conns<D: EventDispatcher, A: IpAddress>(
    state: &mut StackState<D>,
) -> &mut ConnAddrMap<D::IcmpConn, IcmpAddr<A>> {
    #[ipv4addr]
    return &mut state.ip.icmp.v4.conns;
    #[ipv6addr]
    return &mut state.ip.icmp.v6.conns;
}

#[cfg(test)]
mod tests {
    use packet::{Buf, BufferSerializer, Serializer};

    use std::fmt::Debug;
    #[cfg(feature = "udp-icmp-port-unreachable")]
    use std::num::NonZeroU16;

    use super::*;
    use crate::device::{DeviceId, FrameDestination};
    use crate::ip::{receive_ip_packet, Ip, IpExt, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
    use crate::testutil::{
        DummyEventDispatcher, DummyEventDispatcherBuilder, DUMMY_CONFIG_V4, DUMMY_CONFIG_V6,
    };
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
            .encapsulate(<Ipv4 as IpExt>::PacketBuilder::new(
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
            DeviceId::new_ethernet(0),
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

    #[test]
    fn test_should_send_icmpv4_error() {
        let src_ip = DUMMY_CONFIG_V4.local_ip;
        let dst_ip = DUMMY_CONFIG_V4.remote_ip;
        let frame_dst = FrameDestination::Unicast;
        let multicast_ip_1 = Ipv4Addr::new([224, 0, 0, 1]);
        let multicast_ip_2 = Ipv4Addr::new([224, 0, 0, 2]);

        // Should Send.
        assert!(should_send_icmpv4_error(frame_dst, src_ip, dst_ip));

        // Should not send because destined for IP broadcast addr
        assert!(!should_send_icmpv4_error(frame_dst, src_ip, Ipv4Addr::new([255, 255, 255, 255])));

        // Should not send because destined for multicast addr
        assert!(!should_send_icmpv4_error(frame_dst, src_ip, multicast_ip_1));

        // Should not send because Link Layer Broadcast.
        assert!(!should_send_icmpv4_error(FrameDestination::Broadcast, src_ip, dst_ip));

        // Should not send because from unspecified addr
        assert!(!should_send_icmpv4_error(frame_dst, Ipv4::UNSPECIFIED_ADDRESS, dst_ip));

        // Should not send because from loopback addr
        assert!(!should_send_icmpv4_error(frame_dst, Ipv4::LOOPBACK_ADDRESS, dst_ip));

        // Should not send because from global broadcast addr
        assert!(!should_send_icmpv4_error(frame_dst, Ipv4::GLOBAL_BROADCAST_ADDRESS, dst_ip));

        // Should not send because from multicast addr
        assert!(!should_send_icmpv4_error(frame_dst, multicast_ip_2, dst_ip));

        // Should not send because from class e addr
        assert!(!should_send_icmpv4_error(frame_dst, Ipv4Addr::new([240, 0, 0, 1]), dst_ip));
    }

    #[test]
    fn test_should_send_icmpv6_error() {
        let src_ip = DUMMY_CONFIG_V6.local_ip;
        let dst_ip = DUMMY_CONFIG_V6.remote_ip;
        let frame_dst = FrameDestination::Unicast;
        let multicast_ip_1 = Ipv6Addr::new([255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);
        let multicast_ip_2 = Ipv6Addr::new([255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2]);

        // Should Send.
        assert!(should_send_icmpv6_error(frame_dst, src_ip, dst_ip, false));
        assert!(should_send_icmpv6_error(frame_dst, src_ip, dst_ip, true));

        // Should not send because destined for multicast addr,
        // unless exception applies
        assert!(!should_send_icmpv6_error(frame_dst, src_ip, multicast_ip_1, false));
        assert!(should_send_icmpv6_error(frame_dst, src_ip, multicast_ip_1, true));

        // Should not send because Link Layer Broadcast.,
        // unless exception applies
        assert!(!should_send_icmpv6_error(FrameDestination::Broadcast, src_ip, dst_ip, false));
        assert!(should_send_icmpv6_error(FrameDestination::Broadcast, src_ip, dst_ip, true));

        // Should not send because from unspecified addr
        assert!(!should_send_icmpv6_error(frame_dst, Ipv6::UNSPECIFIED_ADDRESS, dst_ip, false));
        assert!(!should_send_icmpv6_error(frame_dst, Ipv6::UNSPECIFIED_ADDRESS, dst_ip, true));

        // Should not send because from loopback addr
        assert!(!should_send_icmpv6_error(frame_dst, Ipv6::LOOPBACK_ADDRESS, dst_ip, false));
        assert!(!should_send_icmpv6_error(frame_dst, Ipv6::LOOPBACK_ADDRESS, dst_ip, true));

        // Should not send because from multicast addr
        assert!(!should_send_icmpv6_error(frame_dst, multicast_ip_2, dst_ip, false));
        assert!(!should_send_icmpv6_error(frame_dst, multicast_ip_2, dst_ip, true));

        // Should not send becuase from multicast addr,
        // even though dest multicast exception applies
        assert!(!should_send_icmpv6_error(
            FrameDestination::Broadcast,
            multicast_ip_2,
            dst_ip,
            false
        ));
        assert!(!should_send_icmpv6_error(
            FrameDestination::Broadcast,
            multicast_ip_2,
            dst_ip,
            true
        ));
        assert!(!should_send_icmpv6_error(frame_dst, multicast_ip_2, multicast_ip_1, false));
        assert!(!should_send_icmpv6_error(frame_dst, multicast_ip_2, multicast_ip_1, true));
    }

    fn test_icmp_connections<I: Ip>() {
        crate::testutil::set_logger_for_test();
        let config = crate::testutil::get_dummy_config::<I::Addr>();
        let mut net =
            crate::testutil::new_dummy_network_from_config("alice", "bob", config.clone());

        let conn = 1;
        let icmp_id = 13;

        new_icmp_connection(net.context("alice"), conn, config.local_ip, config.remote_ip, icmp_id)
            .unwrap();

        let echo_body = vec![1, 2, 3, 4];

        send_icmp_echo_request::<_, I>(net.context("alice"), &conn, 7, &echo_body);

        net.run_until_idle().unwrap();
        assert_eq!(
            *net.context("bob").state().test_counters.get("receive_icmp_packet::echo_request"),
            1
        );
        assert_eq!(
            *net.context("alice").state().test_counters.get("receive_icmp_packet::echo_reply"),
            1
        );
        let replies = net.context("alice").dispatcher_mut().take_icmp_replies(conn);
        assert!(!replies.is_empty());
        let (seq, body) = &replies[0];
        assert_eq!(*seq, 7);
        assert_eq!(*body, echo_body);
    }

    #[test]
    fn test_icmp_connections_v4() {
        test_icmp_connections::<Ipv4>();
    }

    #[test]
    fn test_icmp_connections_v6() {
        test_icmp_connections::<Ipv6>();
    }
}
