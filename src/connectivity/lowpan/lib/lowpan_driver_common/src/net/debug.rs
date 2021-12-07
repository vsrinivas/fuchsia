// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

/// A wrapper around a byte slice that allows it to be printed out like
/// an IPv6 packet for debugging purposes.
pub struct Ipv6PacketDebug<'a>(pub &'a [u8]);

impl<'a> std::fmt::Debug for Ipv6PacketDebug<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "IPv6;")?;

        let mut packet_bytes = self.0;

        let packet = if let Ok(packet) = Ipv6Packet::parse(&mut packet_bytes, ()) {
            packet
        } else {
            return write!(f, "CORRUPT");
        };

        let src_ip = packet.src_ip();
        let dst_ip = packet.dst_ip();

        let (src_port, dst_port) = match packet.proto() {
            Ipv6Proto::Proto(IpProto::Tcp) => {
                let args = TcpParseArgs::new(packet.src_ip(), packet.dst_ip());
                write!(f, "TCP;")?;
                match TcpSegment::parse(&mut packet_bytes, args) {
                    Ok(tcp) => {
                        if tcp.rst() {
                            write!(f, "RST;")?;
                        }
                        if tcp.syn() {
                            write!(f, "SYN;")?;
                        }
                        if tcp.fin() {
                            write!(f, "FIN;")?;
                        }
                        write!(f, "SEQ={};", tcp.seq_num())?;
                        if let Some(ack) = tcp.ack_num() {
                            write!(f, "ACK={};", ack)?;
                        }
                        (Some(tcp.src_port()), Some(tcp.dst_port()))
                    }
                    Err(_) => {
                        write!(f, "CORRUPT;")?;
                        (None, None)
                    }
                }
            }
            Ipv6Proto::Proto(IpProto::Udp) => {
                let args = UdpParseArgs::new(packet.src_ip(), packet.dst_ip());
                write!(f, "UDP;")?;
                match UdpPacket::parse(&mut packet_bytes, args) {
                    Ok(udp) => (udp.src_port(), Some(udp.dst_port())),
                    Err(_) => {
                        write!(f, "CORRUPT;")?;
                        (None, None)
                    }
                }
            }
            Ipv6Proto::Icmpv6 => {
                let args = IcmpParseArgs::new(packet.src_ip(), packet.dst_ip());
                write!(f, "ICMPv6;")?;
                match Icmpv6Packet::parse(&mut packet_bytes, args) {
                    Ok(icmp) => {
                        write!(f, "{:?};", icmp)?;
                    }
                    Err(_) => {
                        write!(f, "CORRUPT;")?;
                    }
                }
                (None, None)
            }
            other_proto => {
                write!(f, "{:?};", other_proto)?;
                (None, None)
            }
        };

        if let Some(port) = src_port {
            write!(f, "src=[{}]:{};", src_ip, port)?;
        } else {
            write!(f, "src=[{}];", src_ip)?;
        }

        if let Some(port) = dst_port {
            write!(f, "dst=[{}]:{}", dst_ip, port)?;
        } else {
            write!(f, "dst=[{}]", dst_ip)?;
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex;

    #[test]
    fn test_ipv6packet_debug() {
        let packet1 = hex::decode("60000000003011fffe800000000000004052fe034ab90ec9ff0200000000000000000000000000fb14e914e90030f54c0000000000010000000000000b5f676f6f676c6563617374045f746370056c6f63616c00000c0001").expect("bad ipv6 hex");
        assert_eq!(
            &format!("{:?}", Ipv6PacketDebug(&packet1)),
            "IPv6;UDP;src=[fe80::4052:fe03:4ab9:ec9]:5353;dst=[ff02::fb]:5353"
        );

        let packet2 = hex::decode("6000000000213a40fddead00beef000095d60909a7d2f8d6fddead00beef000095d60909a7d2f8d780001cb5c31800015468697320697320616e206563686f206d6573736167652100").expect("bad ipv6 hex");
        let packet2_str = format!("{:?}", Ipv6PacketDebug(&packet2));
        assert!(packet2_str.starts_with("IPv6;ICMPv6;EchoRequest"));
        assert!(packet2_str.ends_with(
            ";src=[fdde:ad00:beef:0:95d6:909:a7d2:f8d6];dst=[fdde:ad00:beef:0:95d6:909:a7d2:f8d7]"
        ));

        let packet3 = hex::decode("deadbeef").expect("bad ipv6 hex");
        assert_eq!(&format!("{:?}", Ipv6PacketDebug(&packet3)), "IPv6;CORRUPT");
    }
}
