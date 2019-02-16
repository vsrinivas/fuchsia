// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Control Message Protocol (ICMP).

use std::mem;

use log::trace;
use packet::{BufferMut, BufferSerializer, Serializer};

use crate::ip::{send_ip_packet, IpAddr, IpProto, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
use crate::wire::icmp::{
    IcmpDestUnreachable, IcmpPacketBuilder, IcmpParseArgs, Icmpv4DestUnreachableCode, Icmpv4Packet,
    Icmpv6DestUnreachableCode, Icmpv6Packet, Icmpv6ParameterProblem, Icmpv6ParameterProblemCode,
};
use crate::{Context, EventDispatcher};

/// Receive an ICMP message in an IP packet.
pub fn receive_icmp_packet<D: EventDispatcher, A: IpAddr, B: BufferMut>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    buffer: B,
) {
    trace!("receive_icmp_packet({}, {})", src_ip, dst_ip);

    specialize_ip_addr!(
        fn receive_icmp_packet<D, B>(ctx: &mut Context<D>, src_ip: Self, dst_ip: Self, buffer: B)
        where
            D: EventDispatcher,
            B: BufferMut,
        {
            Ipv4Addr => {
                let mut buffer = buffer;
                let packet = match buffer.parse_with::<_, Icmpv4Packet<_>>(IcmpParseArgs::new(src_ip, dst_ip)) {
                    Ok(packet) => packet,
                    Err(err) => return, // TODO(joshlf): Do something else here?
                };

                match packet {
                    Icmpv4Packet::EchoRequest(echo_request) => {
                        let req = *echo_request.message();
                        let code = echo_request.code();
                        // drop packet so we can re-use the underlying buffer
                        mem::drop(echo_request);

                        increment_counter!(ctx, "receive_icmp_packet::echo_request");

                        // we're responding to the sender, so these are flipped
                        let (src_ip, dst_ip) = (dst_ip, src_ip);
                        send_ip_packet(
                            ctx,
                            dst_ip,
                            IpProto::Icmp,
                            |src_ip| BufferSerializer::new_vec(buffer).encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(src_ip, dst_ip, code, req.reply())),
                        );
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
            Ipv6Addr => {
                let mut buffer = buffer;
                let packet = match buffer.parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(src_ip, dst_ip)) {
                    Ok(packet) => packet,
                    Err(err) => return, // TODO(joshlf): Do something else here?
                };

                match packet {
                    Icmpv6Packet::EchoRequest(echo_request) => {
                        let req = *echo_request.message();
                        let code = echo_request.code();
                        // drop packet so we can re-use the underlying buffer
                        mem::drop(echo_request);

                        increment_counter!(ctx, "receive_icmp_packet::echo_request");

                        // we're responding to the sender, so these are flipped
                        let (src_ip, dst_ip) = (dst_ip, src_ip);
                        send_ip_packet(
                            ctx,
                            dst_ip,
                            IpProto::Icmp,
                            |src_ip| BufferSerializer::new_vec(buffer).encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(src_ip, dst_ip, code, req.reply())),
                        );
                    }
                    Icmpv6Packet::EchoReply(echo_reply) => {
                        increment_counter!(ctx, "receive_icmp_packet::echo_reply");
                        trace!("receive_icmp_packet: Received an EchoReply message");
                    }
                    _ => log_unimplemented!(
                        (),
                        "ip::icmp::receive_icmp_packet: Not implemented for this packet type"
                    )
                }
            }
        }
    );

    A::receive_icmp_packet(ctx, src_ip, dst_ip, buffer)
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
pub fn send_icmp_protocol_unreachable<D: EventDispatcher, A: IpAddr, B: BufferMut>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    original_packet: B,
    header_len: usize,
) {
    increment_counter!(ctx, "send_icmp_protocol_unreachable");

    specialize_ip_addr! {
        fn make_packet_builder<D, B>(ctx: &mut Context<D>, src_ip: Self, dst_ip: Self, original_packet: B, header_len: usize)
        where
            D: EventDispatcher,
            B: BufferMut,
        {
            Ipv4Addr => {
                send_icmpv4_dest_unreachable(
                    ctx,
                    src_ip,
                    dst_ip,
                    Icmpv4DestUnreachableCode::DestProtocolUnreachable,
                    original_packet,
                    header_len,
                );
            }
            Ipv6Addr => {
                // Per RFC 4443, body contains as much of the original body as
                // possible without exceeding IPv6 minimum MTU.
                let mut original_packet = original_packet;
                original_packet.shrink_back_to(crate::ip::IPV6_MIN_MTU);
                // TODO(joshlf): The source address should probably be fixed rather than
                // looked up in the routing table.
                send_ip_packet(ctx, dst_ip, IpProto::Icmpv6, |src_ip| {
                    BufferSerializer::new_vec(original_packet).encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                        src_ip,
                        dst_ip,
                        Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
                        // Per RFC 4443, the pointer refers to the first byte of
                        // the packet whose Next Header field was unrecognized.
                        // It is measured as an offset from the beginning of the
                        // first IPv6 header. E.g., a pointer of 40 (the length
                        // of a single IPv6 header) would indicate that the Next
                        // Header field from that header - and hence of the
                        // first encapsulated packet - was unrecognized.
                        Icmpv6ParameterProblem::new({
                            // TODO(joshlf): Use TryInto::try_into once it's stable.
                            assert!(header_len <= u32::max_value() as usize);
                            header_len as u32
                        }),
                    ))
                });
            }
        }
    }

    // NOTE(joshlf): src_ip and dst_ip swapped since we're responding
    A::make_packet_builder(ctx, dst_ip, src_ip, original_packet, header_len);
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
pub fn send_icmp_port_unreachable<D: EventDispatcher, A: IpAddr, B: BufferMut>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    original_packet: B,
    ipv4_header_len: usize,
) {
    increment_counter!(ctx, "send_icmp_port_unreachable");

    specialize_ip_addr! {
        fn make_packet_builder<D, B>(ctx: &mut Context<D>, src_ip: Self, dst_ip: Self, original_packet: B, ipv4_header_len: usize)
        where
            D: EventDispatcher,
            B: BufferMut,
        {
            Ipv4Addr => {
                send_icmpv4_dest_unreachable(
                    ctx,
                    src_ip,
                    dst_ip,
                    Icmpv4DestUnreachableCode::DestPortUnreachable,
                    original_packet,
                    ipv4_header_len,
                );
            }
            Ipv6Addr => {
                send_icmpv6_dest_unreachable(
                    ctx,
                    src_ip,
                    dst_ip,
                    Icmpv6DestUnreachableCode::PortUnreachable,
                    original_packet,
                );
            }
        }
    }

    // NOTE(joshlf): src_ip and dst_ip swapped since we're responding
    A::make_packet_builder(ctx, dst_ip, src_ip, original_packet, ipv4_header_len);
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
pub fn send_icmp_net_unreachable<D: EventDispatcher, A: IpAddr, B: BufferMut>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    original_packet: B,
    ipv4_header_len: usize,
) {
    increment_counter!(ctx, "send_icmp_net_unreachable");

    specialize_ip_addr! {
        fn make_packet_builder<D, B>(ctx: &mut Context<D>, src_ip: Self, dst_ip: Self, original_packet: B, ipv4_header_len: usize)
        where
            D: EventDispatcher,
            B: BufferMut,
        {
            Ipv4Addr => {
                send_icmpv4_dest_unreachable(
                    ctx,
                    src_ip,
                    dst_ip,
                    Icmpv4DestUnreachableCode::DestNetworkUnreachable,
                    original_packet,
                    ipv4_header_len,
                );
            }
            Ipv6Addr => {
                send_icmpv6_dest_unreachable(
                    ctx,
                    src_ip,
                    dst_ip,
                    Icmpv6DestUnreachableCode::NoRoute,
                    original_packet,
                );
            }
        }
    }

    // NOTE(joshlf): src_ip and dst_ip swapped since we're responding
    // TODO(joshlf): When we finally get around to setting src_ip properly (see
    // note in send_icmpvX_dest_unreachable), we need to make sure we use our
    // source IP rather than the original packet's destination IP (which won't
    // necessarily be the same if we have forwarding enabled).
    A::make_packet_builder(ctx, dst_ip, src_ip, original_packet, ipv4_header_len);
}

fn send_icmpv4_dest_unreachable<D: EventDispatcher, B: BufferMut>(
    ctx: &mut Context<D>,
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
    // TODO(joshlf): The source address should probably be fixed rather than
    // looked up in the routing table.
    send_ip_packet(ctx, dst_ip, IpProto::Icmp, |src_ip| {
        BufferSerializer::new_vec(original_packet).encapsulate(
            IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                src_ip,
                dst_ip,
                code,
                IcmpDestUnreachable::default(),
            ),
        )
    });
}

fn send_icmpv6_dest_unreachable<D: EventDispatcher, B: BufferMut>(
    ctx: &mut Context<D>,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    code: Icmpv6DestUnreachableCode,
    original_packet: B,
) {
    // Per RFC 4443, body contains as much of the original body as possible
    // without exceeding IPv6 minimum MTU.
    let mut original_packet = original_packet;
    original_packet.shrink_back_to(crate::ip::IPV6_MIN_MTU);
    // TODO(joshlf): The source address should probably be fixed rather than
    // looked up in the routing table.
    send_ip_packet(ctx, dst_ip, IpProto::Icmpv6, |src_ip| {
        BufferSerializer::new_vec(original_packet).encapsulate(
            IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                dst_ip,
                code,
                IcmpDestUnreachable::default(),
            ),
        )
    });
}

#[cfg(test)]
mod tests {
    use packet::{Buf, BufferSerializer, Serializer};

    use std::fmt::Debug;
    use std::num::NonZeroU16;

    use super::*;
    use crate::device::DeviceId;
    use crate::ip::{receive_ip_packet, IpExt, Ipv4, Ipv4Addr};
    use crate::testutil::{DummyEventDispatcherBuilder, DUMMY_CONFIG};
    use crate::wire::icmp::{
        IcmpEchoRequest, IcmpMessage, IcmpPacket, IcmpUnusedCode, MessageBody,
    };
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
                DUMMY_CONFIG.remote_ip,
                dst_ip,
                ttl,
                proto,
            ))
            .serialize_outer();

        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG).build();
        // currently only used by test_ttl_exceeded
        ctx.state().ip.v4.forward = true;
        receive_ip_packet::<_, _, Ipv4>(&mut ctx, DeviceId::new_ethernet(1), buffer);

        for counter in assert_counters {
            assert!(ctx.state().test_counters.get(counter) > &0, "counter at zero: {}", counter);
        }

        assert_eq!(ctx.dispatcher().frames_sent().len(), 1);
        let (src_mac, dst_mac, src_ip, dst_ip, message, code) =
            crate::testutil::parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv4, _, M, _>(
                &ctx.dispatcher().frames_sent()[0].1,
                f,
            )
            .unwrap();

        assert_eq!(src_mac, DUMMY_CONFIG.local_mac);
        assert_eq!(dst_mac, DUMMY_CONFIG.remote_mac);
        assert_eq!(src_ip, DUMMY_CONFIG.local_ip);
        assert_eq!(dst_ip, DUMMY_CONFIG.remote_ip);
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
                DUMMY_CONFIG.remote_ip,
                DUMMY_CONFIG.local_ip,
                IcmpUnusedCode,
                req,
            ))
            .serialize_outer();
        test_receive_ip_packet(
            buffer.as_mut(),
            DUMMY_CONFIG.local_ip,
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
            DUMMY_CONFIG.local_ip,
            64,
            IpProto::Other(255),
            &["send_icmp_protocol_unreachable", "send_ip_packet"],
            IcmpDestUnreachable::default(),
            Icmpv4DestUnreachableCode::DestProtocolUnreachable,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }

    #[test]
    fn test_port_unreachable() {
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
                DUMMY_CONFIG.remote_ip,
                DUMMY_CONFIG.local_ip,
                None,
                NonZeroU16::new(1234).unwrap(),
            ))
            .serialize_outer();
        test_receive_ip_packet(
            buffer.as_mut(),
            DUMMY_CONFIG.local_ip,
            64,
            IpProto::Udp,
            &["send_icmp_port_unreachable", "send_ip_packet"],
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
            &["send_icmp_net_unreachable", "send_ip_packet"],
            IcmpDestUnreachable::default(),
            Icmpv4DestUnreachableCode::DestNetworkUnreachable,
            // ensure packet is truncated to the right length
            |packet| assert_eq!(packet.original_packet().bytes().len(), 84),
        );
    }
}
