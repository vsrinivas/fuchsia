// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of Internet Control Message Protocol (ICMP)

use std::mem;

use log::{log, trace};

use crate::ip::{send_ip_packet, IpAddr, IpProto};
use crate::wire::icmp::{IcmpPacket, Icmpv4Packet};
use crate::wire::BufferAndRange;
use crate::StackState;

/// Receive an ICMP message in an IP packet.
pub fn receive_icmp_packet<A: IpAddr, B: AsRef<[u8]> + AsMut<[u8]>>(
    state: &mut StackState, src_ip: A, dst_ip: A, buffer: BufferAndRange<B>,
) -> bool {
    trace!("receive_icmp_packet({}, {})", src_ip, dst_ip);

    // serialize_ip! can't handle trait bounds with type arguments, so create
    // AsMutU8 which is equivalent to AsMut<[u8]>, but without the type
    // arguments. Ew.
    trait AsU8: AsRef<[u8]> + AsMut<[u8]> {}
    impl<A: AsRef<[u8]> + AsMut<[u8]>> AsU8 for A {}
    specialize_ip_addr!(
        fn receive_icmp_packet<B>(state: &mut StackState, src_ip: Self, dst_ip: Self, buffer: BufferAndRange<B>) -> bool
        where
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
                        increment_counter!(state, "receive_icmp_packet::echo_request");
                        let req = *echo_request.message();
                        let code = echo_request.code();
                        // drop packet so we can re-use the underlying buffer
                        mem::drop(echo_request);
                        // slice the buffer to be only the body range
                        buffer.slice(body_range);

                        // we're responding to the sender, so these are flipped
                        let (src_ip, dst_ip) = (dst_ip, src_ip);
                        if false {
                            send_ip_packet(
                                state,
                                dst_ip,
                                IpProto::Icmp,
                                |src_ip, min_prefix_size, min_body_and_padding_size| {
                                    // TODO: Check IcmpPacket::serialize_len to
                                    // make sure we've given enough space in the
                                    // buffer. There definitely is since Echo
                                    // Requests and Echo Replies have the same
                                    // format, but we shouldn't be relying on
                                    // that for correctness.
                                    let buffer = IcmpPacket::serialize(buffer, src_ip, dst_ip, code, req);
                                    // The current buffer may not have enough
                                    // prefix space for all of the link-layer
                                    // headers or for the post-body padding, so
                                    // use ensure_prefix_padding to ensure that
                                    // we are using a buffer with sufficient
                                    // space.

                                    // TODO(joshlf): Fix monomorphization
                                    // overflow error that happens when this
                                    // line is uncommented.
                                    // ensure_prefix_padding(buffer, min_prefix_size, min_body_and_padding_size)
                                    buffer
                                },
                            );
                        }
                        log_unimplemented!(false, "ip::icmp::receive_icmp_packet: Not implemented for this packet type")
                    }
                    Icmpv4Packet::EchoReply(echo_reply) => {
                        increment_counter!(state, "receive_icmp_packet::echo_reply");
                        trace!("receive_icmp_packet: Received an EchoReply message");
                        true
                    }
                    _ => log_unimplemented!(
                        false,
                        "ip::icmp::receive_icmp_packet: Not implemented for this packet type"
                    ),
                }
            }
            Ipv6Addr => { log_unimplemented!(false, "ip::icmp::receive_icmp_packet: Not implemented for IPv6") }
        }
    );

    A::receive_icmp_packet(state, src_ip, dst_ip, buffer)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ip::{Ip, Ipv4, Ipv4Addr};

    // TODO(joshlf): Un-ignore once we've fixed the monomorphization overflow
    // issue
    #[ignore]
    #[test]
    fn test_send_echo_request() {
        use crate::ip::testdata::icmp_echo::*;

        let mut state: StackState = Default::default();
        let src = <Ipv4 as Ip>::LOOPBACK_ADDRESS;
        let dst = Ipv4Addr::new([192, 168, 1, 5]);
        let mut bytes = REQUEST_IP_PACKET_BYTES.to_owned();
        let len = bytes.len();
        let buf = BufferAndRange::new(&mut bytes, 20..len);

        receive_icmp_packet(&mut state, src, dst, buf);

        assert_eq!(
            state.test_counters.get("receive_icmp_packet::echo_request"),
            &1
        );

        // Check that the echo request was replied to.
        assert_eq!(state.test_counters.get("send_ip_packet"), &1);
        assert_eq!(state.test_counters.get("send_ip_packet::loopback"), &1);
        assert_eq!(state.test_counters.get("dispatch_receive_ip_packet"), &1);
        assert_eq!(
            state.test_counters.get("receive_icmp_packet::echo_reply"),
            &1
        );
    }
}
