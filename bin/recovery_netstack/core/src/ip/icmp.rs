// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Internet Control Message Protocol (ICMP).

use std::mem;

use log::trace;

use crate::ip::{send_ip_packet, IpAddr, IpProto, Ipv4, Ipv6};
use crate::wire::icmp::{IcmpPacketSerializer, Icmpv4Packet, Icmpv6Packet};
use crate::wire::{BufferAndRange, SerializationRequest};
use crate::{Context, EventDispatcher};

/// Receive an ICMP message in an IP packet.
pub fn receive_icmp_packet<D: EventDispatcher, A: IpAddr, B: AsRef<[u8]> + AsMut<[u8]>>(
    ctx: &mut Context<D>, src_ip: A, dst_ip: A, buffer: BufferAndRange<B>,
) -> bool {
    trace!("receive_icmp_packet({}, {})", src_ip, dst_ip);

    // specialize_ip_addr! can't handle trait bounds with type arguments, so
    // create AsMutU8 which is equivalent to AsMut<[u8]>, but without the type
    // arguments. Ew.
    trait AsU8: AsRef<[u8]> + AsMut<[u8]> {}
    impl<A: AsRef<[u8]> + AsMut<[u8]>> AsU8 for A {}
    specialize_ip_addr!(
        fn receive_icmp_packet<D, B>(ctx: &mut Context<D>, src_ip: Self, dst_ip: Self, buffer: BufferAndRange<B>) -> bool
        where
            D: EventDispatcher,
            B: AsU8,
        {
            Ipv4Addr => {
                let mut buffer = buffer;
                let (packet, body_range) = match Icmpv4Packet::parse(buffer.as_mut(), src_ip, dst_ip) {
                    Ok(packet) => packet,
                    Err(err) => return false,
                };

                match packet {
                    Icmpv4Packet::EchoRequest(echo_request) => {
                        let req = *echo_request.message();
                        let code = echo_request.code();
                        // drop packet so we can re-use the underlying buffer
                        mem::drop(echo_request);
                        // slice the buffer to be only the body range
                        buffer.slice(body_range);

                        increment_counter!(ctx, "receive_icmp_packet::echo_request");

                        // we're responding to the sender, so these are flipped
                        let (src_ip, dst_ip) = (dst_ip, src_ip);
                        send_ip_packet(ctx, dst_ip, IpProto::Icmp, |src_ip| {
                            buffer.encapsulate(IcmpPacketSerializer::<Ipv4, B, _>::new(
                                src_ip,
                                dst_ip,
                                code,
                                req.reply(),
                            ))
                        });
                        true
                    }
                    Icmpv4Packet::EchoReply(echo_reply) => {
                        increment_counter!(ctx, "receive_icmp_packet::echo_reply");
                        trace!("receive_icmp_packet: Received an EchoReply message");
                        true
                    }
                    _ => log_unimplemented!(
                        false,
                        "ip::icmp::receive_icmp_packet: Not implemented for this packet type"
                    ),
                }
            }
            Ipv6Addr => {
                let mut buffer = buffer;
                let (packet, body_range) = match Icmpv6Packet::parse(buffer.as_mut(), src_ip, dst_ip) {
                    Ok(packet) => packet,
                    Err(err) => {
                        return false
                    },
                };

                match packet {
                    Icmpv6Packet::EchoRequest(echo_request) => {
                        let req = *echo_request.message();
                        let code = echo_request.code();
                        // drop packet so we can re-use the underlying buffer
                        mem::drop(echo_request);
                        // slice the buffer to be only the body range
                        buffer.slice(body_range);

                        increment_counter!(ctx, "receive_icmp_packet::echo_request");

                        // we're responding to the sender, so these are flipped
                        let (src_ip, dst_ip) = (dst_ip, src_ip);
                        send_ip_packet(ctx, dst_ip, IpProto::Icmp, |src_ip| {
                            buffer.encapsulate(IcmpPacketSerializer::<Ipv6, B, _>::new(
                                src_ip,
                                dst_ip,
                                code,
                                req.reply(),
                            ))
                        });
                        true
                    }
                    Icmpv6Packet::EchoReply(echo_reply) => {
                        increment_counter!(ctx, "receive_icmp_packet::echo_reply");
                        trace!("receive_icmp_packet: Received an EchoReply message");
                        true
                    }
                    _ => log_unimplemented!(
                        false,
                        "ip::icmp::receive_icmp_packet: Not implemented for this packet type"
                    )
                }
            }
        }
    );

    A::receive_icmp_packet(ctx, src_ip, dst_ip, buffer)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ip::{Ip, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
    use crate::testutil::DummyEventDispatcher;
    use crate::Context;

    #[test]
    fn test_send_echo_request_v4() {
        use crate::ip::testdata::icmp_echo_v4::*;

        let mut ctx = Context::new(Default::default(), DummyEventDispatcher::default());
        let src = <Ipv4 as Ip>::LOOPBACK_ADDRESS;
        let dst = Ipv4Addr::new([192, 168, 1, 5]);
        let mut bytes = REQUEST_IP_PACKET_BYTES.to_owned();
        let len = bytes.len();
        let buf = BufferAndRange::new_from(&mut bytes, 20..len);

        receive_icmp_packet(&mut ctx, src, dst, buf);
        assert_eq!(
            ctx.state()
                .test_counters
                .get("receive_icmp_packet::echo_request"),
            &1
        );

        // Check that the echo request was replied to.
        assert_eq!(ctx.state().test_counters.get("send_ip_packet"), &1);
        assert_eq!(
            ctx.state().test_counters.get("send_ip_packet::loopback"),
            &1
        );
        assert_eq!(
            ctx.state().test_counters.get("dispatch_receive_ip_packet"),
            &1
        );
        assert_eq!(
            ctx.state()
                .test_counters
                .get("receive_icmp_packet::echo_reply"),
            &1
        );
    }

    #[test]
    fn test_send_echo_request_v6() {
        use crate::ip::testdata::icmp_echo_v6::*;

        let mut ctx = Context::new(Default::default(), DummyEventDispatcher::default());
        let src = <Ipv6 as Ip>::LOOPBACK_ADDRESS;
        let dst = Ipv6Addr::new([0xfe, 0xc0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
        let mut bytes = REQUEST_IP_PACKET_BYTES.to_owned();
        let len = bytes.len();
        let buf = BufferAndRange::new_from(&mut bytes, 40..len);

        receive_icmp_packet(&mut ctx, src, dst, buf);
        assert_eq!(
            ctx.state()
                .test_counters
                .get("receive_icmp_packet::echo_request"),
            &1
        );

        // Check that the echo request was replied to.
        assert_eq!(ctx.state().test_counters.get("send_ip_packet"), &1);
        assert_eq!(
            ctx.state().test_counters.get("send_ip_packet::loopback"),
            &1
        );
        assert_eq!(
            ctx.state().test_counters.get("dispatch_receive_ip_packet"),
            &1
        );
        assert_eq!(
            ctx.state()
                .test_counters
                .get("receive_icmp_packet::echo_reply"),
            &1
        );
    }
}
