// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Packet parsing helpers.

#![deny(missing_docs)]

use packet::ParsablePacket as _;

/// Parse a packet as a DHCPv6 message.
///
/// Returns `None` if the packet is not DHCPv6 in UDP in IPv6 in Ethernet.
///
/// # Panics
///
/// Panics if parsing fails at any layer.
pub fn parse_dhcpv6(mut buf: &[u8]) -> Option<packet_formats_dhcp::v6::Message<'_, &[u8]>> {
    let eth = packet_formats::ethernet::EthernetFrame::parse(
        &mut buf,
        packet_formats::ethernet::EthernetFrameLengthCheck::Check,
    )
    .expect("failed to parse ethernet frame");

    match eth.ethertype().expect("ethertype missing") {
        packet_formats::ethernet::EtherType::Ipv6 => {}
        packet_formats::ethernet::EtherType::Ipv4
        | packet_formats::ethernet::EtherType::Arp
        | packet_formats::ethernet::EtherType::Other(_) => {
            return None;
        }
    }

    let (mut ipv6_body, src_ip, dst_ip, proto, _ttl) =
        packet_formats::testutil::parse_ip_packet::<net_types::ip::Ipv6>(buf)
            .expect("error parsing IPv6 packet");
    if proto != packet_formats::ip::Ipv6Proto::Proto(packet_formats::ip::IpProto::Udp) {
        return None;
    }

    let udp_packet = packet_formats::udp::UdpPacket::parse(
        &mut ipv6_body,
        packet_formats::udp::UdpParseArgs::new(src_ip, dst_ip),
    )
    .expect("error parsing UDP datagram");

    let mut udp_body = udp_packet.into_body();
    Some(
        packet_formats_dhcp::v6::Message::parse(&mut udp_body, ())
            .expect("error parsing as DHCPv6 message"),
    )
}
