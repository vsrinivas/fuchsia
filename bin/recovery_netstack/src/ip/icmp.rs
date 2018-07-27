// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of Internet Control Message Protocol (ICMP)

use ip::{send_ip_packet, IpAddr, IpProto};
use std::cmp::max;
use wire::icmp::{Icmpv4Body, Icmpv4Packet};
use wire::BufferAndRange;
use StackState;

/// Receive an ICMP message in an IP packet.
pub fn receive_icmp_packet<A: IpAddr, B: AsMut<[u8]>>(
    state: &mut StackState,
    src_ip: A,
    dst_ip: A,
    mut buffer: BufferAndRange<B>,
) -> bool {
    trace!("receive_icmp_packet({}, {})", src_ip, dst_ip);
    let packet = match Icmpv4Packet::parse(buffer.as_mut()) {
        Ok(packet) => packet,
        Err(err) => return false,
    };

    match packet.body() {
        Icmpv4Body::EchoRequest(echo_request) => {
            increment_counter!(state, "receive_icmp_packet::echo_request");
            send_ip_packet(
                state,
                src_ip,
                IpProto::Icmp,
                |ip, min_prefix_size, min_body_and_padding_size| {
                    let reply = echo_request.reply();
                    let buffer_len = min_prefix_size + max(min_body_and_padding_size, reply.size());
                    let data = vec![0; buffer_len];
                    let buffer = BufferAndRange::new(data, min_prefix_size..buffer_len);
                    reply.serialize(buffer)
                },
            );
            true
        }
        Icmpv4Body::EchoReply(echo_reply) => {
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

#[cfg(test)]
mod tests {
    use super::*;
    use ip::{Ip, Ipv4, Ipv4Addr};

    #[test]
    fn test_send_echo_request() {
        use ip::testdata::icmp_echo::*;

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
