// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

/// A single IPv6 packet matcher rule.
///
/// This struct can match against the IP protocol, local address/port, and remote address/port.
/// It is used by Ipv6PacketMatcher for implementing a small firewall.
#[derive(Debug, Default, Hash, Clone, Eq, PartialEq)]
pub struct Ipv6PacketMatcherRule {
    pub proto: Option<Ipv6Proto>,

    pub local_port: Option<NonZeroU16>,
    pub local_address: Subnet,

    pub remote_port: Option<NonZeroU16>,
    pub remote_address: Subnet,
}

impl Ipv6PacketMatcherRule {
    /// Make a new matching rule that matches packets that are
    /// related to this inbound packet.
    pub fn try_from_inbound_packet(packet: &[u8]) -> Result<Ipv6PacketMatcherRule, Error> {
        let (proto, src_addr, dst_addr, src_port, dst_port) = Self::decode_packet(packet)?;

        Ok(Ipv6PacketMatcherRule {
            proto: Some(proto),
            local_port: dst_port,
            local_address: Subnet::from(Into::<std::net::Ipv6Addr>::into(dst_addr)),
            remote_port: src_port,
            remote_address: Subnet::from(Into::<std::net::Ipv6Addr>::into(src_addr)),
        })
    }

    /// Decodes the protocol, source address, destination address, source port,
    /// destination port
    fn decode_packet(
        mut packet_bytes: &[u8],
    ) -> Result<(Ipv6Proto, Ipv6Addr, Ipv6Addr, Option<NonZeroU16>, Option<NonZeroU16>), Error>
    {
        let packet =
            Ipv6Packet::parse(&mut packet_bytes, ()).context("failed to parse IPv6 packet")?;

        let proto = packet.proto();
        let src_ip = packet.src_ip().ipv6_bytes().into();
        let dst_ip = packet.dst_ip().ipv6_bytes().into();

        let (src_port, dst_port) = match packet.proto() {
            Ipv6Proto::Proto(IpProto::Tcp) => {
                let args = TcpParseArgs::new(packet.src_ip(), packet.dst_ip());
                let tcp = TcpSegment::parse(&mut packet_bytes, args)
                    .context("failed to parse TCP segment")?;
                (Some(tcp.src_port()), Some(tcp.dst_port()))
            }
            Ipv6Proto::Proto(IpProto::Udp) => {
                let args = UdpParseArgs::new(packet.src_ip(), packet.dst_ip());
                let udp = UdpPacket::parse(&mut packet_bytes, args)
                    .context("failed to parse UDP packet")?;
                (udp.src_port(), Some(udp.dst_port()))
            }
            _ => (None, None),
        };

        Ok((proto, src_ip, dst_ip, src_port, dst_port))
    }

    /// Helper function for matching various parts of an IPv6 packet
    /// to the values associated with the rule.
    fn match_parts(
        &self,
        proto: Ipv6Proto,
        local_addr: Ipv6Addr,
        remote_addr: Ipv6Addr,
        local_port: Option<NonZeroU16>,
        remote_port: Option<NonZeroU16>,
    ) -> bool {
        self.proto.unwrap_or(proto) == proto
            && self.local_address.contains(&local_addr.into())
            && self.remote_address.contains(&remote_addr.into())
            && self.local_port.or(local_port) == local_port
            && self.remote_port.or(remote_port) == remote_port
    }

    /// Determines if an inbound packet matches this rule.
    pub fn match_inbound_packet(&self, packet: &[u8]) -> bool {
        let (proto, remote_addr, local_addr, remote_port, local_port) =
            if let Ok(x) = Self::decode_packet(packet) {
                x
            } else {
                return false;
            };

        self.match_parts(proto, local_addr, remote_addr, local_port, remote_port)
    }

    /// Determines if an outbound packet matches this rule.
    pub fn match_outbound_packet(&self, packet: &[u8]) -> bool {
        let (proto, local_addr, remote_addr, local_port, remote_port) =
            if let Ok(x) = Self::decode_packet(packet) {
                x
            } else {
                return false;
            };

        self.match_parts(proto, local_addr, remote_addr, local_port, remote_port)
    }
}

/// A wrapper around a set of [`Ipv6PacketMatcherRule`]s, with methods for making
/// it easy to use.
#[derive(Debug, Default, Clone, Eq, PartialEq)]
pub struct Ipv6PacketMatcher(pub HashSet<Ipv6PacketMatcherRule>);

impl Deref for Ipv6PacketMatcher {
    type Target = HashSet<Ipv6PacketMatcherRule>;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for Ipv6PacketMatcher {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl Ipv6PacketMatcher {
    /// Makes a matching rule from the given inbound packet and adds it to the set.
    pub fn update_with_inbound_packet(&mut self, packet: &[u8]) -> Result<(), Error> {
        self.0.insert(Ipv6PacketMatcherRule::try_from_inbound_packet(packet)?);
        Ok(())
    }

    /// If there is a matching rule in the set for this inbound packet, remove it.
    pub fn clear_with_inbound_packet(&mut self, packet: &[u8]) -> bool {
        let mut did_match = false;
        self.0.retain(|rule| {
            if rule.match_inbound_packet(packet) {
                did_match = true;
                false
            } else {
                true
            }
        });
        did_match
    }

    /// Determines if an inbound packet matches at least one of the rules in the set.
    pub fn match_inbound_packet(&self, packet: &[u8]) -> bool {
        for rule in self.0.iter() {
            if rule.match_inbound_packet(packet) {
                return true;
            }
        }
        return false;
    }

    /// Determines if an outbound packet matches at least one of the rules in the set.
    pub fn match_outbound_packet(&self, packet: &[u8]) -> bool {
        for rule in self.0.iter() {
            if rule.match_outbound_packet(packet) {
                return true;
            }
        }
        return false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex;

    #[test]
    fn test_ipv6packet_matcher_rule_corrupt() {
        let packet3 = hex::decode("deadbeef").expect("bad ipv6 hex");
        assert!(!Ipv6PacketMatcherRule::default().match_inbound_packet(&packet3));
        assert!(!Ipv6PacketMatcherRule::default().match_outbound_packet(&packet3));
    }

    #[test]
    fn test_ipv6packet_matcher_rule_udp() {
        let packet1 = hex::decode("60000000003011fffe800000000000004052fe034ab90ec9ff0200000000000000000000000000fb14e914e90030f54c0000000000010000000000000b5f676f6f676c6563617374045f746370056c6f63616c00000c0001").expect("bad ipv6 hex");
        let packet1_rule =
            Ipv6PacketMatcherRule::try_from_inbound_packet(&packet1).expect("bad ipv6 packet");

        assert!(packet1_rule.match_inbound_packet(&packet1));
        assert!(!packet1_rule.match_outbound_packet(&packet1));

        let packet2 = hex::decode("6000000000213a40fddead00beef000095d60909a7d2f8d6fddead00beef000095d60909a7d2f8d780001cb5c31800015468697320697320616e206563686f206d6573736167652100").expect("bad ipv6 hex");
        assert!(!packet1_rule.match_inbound_packet(&packet2));
        assert!(!packet1_rule.match_outbound_packet(&packet2));
    }

    #[test]
    fn test_ipv6packet_matcher_rule_icmpv6() {
        let packet2 = hex::decode("6000000000213a40fddead00beef000095d60909a7d2f8d6fddead00beef000095d60909a7d2f8d780001cb5c31800015468697320697320616e206563686f206d6573736167652100").expect("bad ipv6 hex");
        let packet2_rule =
            Ipv6PacketMatcherRule::try_from_inbound_packet(&packet2).expect("bad ipv6 packet");

        assert!(packet2_rule.match_inbound_packet(&packet2));
        assert!(!packet2_rule.match_outbound_packet(&packet2));

        let packet1 = hex::decode("60000000003011fffe800000000000004052fe034ab90ec9ff0200000000000000000000000000fb14e914e90030f54c0000000000010000000000000b5f676f6f676c6563617374045f746370056c6f63616c00000c0001").expect("bad ipv6 hex");

        assert!(!packet2_rule.match_inbound_packet(&packet1));
        assert!(!packet2_rule.match_outbound_packet(&packet1));

        let packet3 = hex::decode("6000000000213a40fddead00beef000095d60909a7d2f8d7fddead00beef000095d60909a7d2f8d680001cb5c31800015468697320697320616e206563686f206d6573736167652100").expect("bad ipv6 hex");
        assert!(!packet2_rule.match_inbound_packet(&packet3));
        assert!(
            packet2_rule.match_outbound_packet(&packet3),
            "failed to match outbound variant: {:?}",
            Ipv6PacketDebug(&packet3)
        );
    }
}
