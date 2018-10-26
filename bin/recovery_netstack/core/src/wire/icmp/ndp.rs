// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Messages used for NDP (ICMPv6).

use std::marker::PhantomData;
use std::ops::Range;

use byteorder::{ByteOrder, NetworkEndian};
use zerocopy::{ByteSlice, LayoutVerified, Unaligned};

use crate::error::ParseError;
use crate::ip::{Ipv6, Ipv6Addr};
use crate::wire::util;

use super::{IcmpIpExt, IcmpUnusedCode};

pub type Options<B> = util::Options<B, options::NdpOptionImpl>;

/// An NDP Router Solicitation.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct RouterSolicitation {
    _reserved: [u8; 4],
}
impl_from_bytes_as_bytes_unaligned!(RouterSolicitation);
impl_icmp_message!(
    Ipv6,
    RouterSolicitation,
    RouterSolicitation,
    IcmpUnusedCode,
    Options<B>
);

/// An NDP Router Advertisment.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct RouterAdvertisment {
    current_hop_limit: u8,
    configuration_mo: u8,
    router_lifetime: [u8; 2],
    reachable_time: [u8; 4],
    retransmit_timer: [u8; 4],
}
impl_from_bytes_as_bytes_unaligned!(RouterAdvertisment);
impl_icmp_message!(
    Ipv6,
    RouterAdvertisment,
    RouterAdvertisment,
    IcmpUnusedCode,
    Options<B>
);

impl RouterAdvertisment {
    pub fn router_lifetime(&self) -> u16 {
        NetworkEndian::read_u16(&self.router_lifetime)
    }

    pub fn reachable_time(&self) -> u32 {
        NetworkEndian::read_u32(&self.reachable_time)
    }

    pub fn retransmit_timer(&self) -> u32 {
        NetworkEndian::read_u32(&self.retransmit_timer)
    }
}

/// An NDP Neighbor Solicitation.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct NeighborSolicitation {
    _reserved: [u8; 4],
    target_address: Ipv6Addr,
}
impl_from_bytes_as_bytes_unaligned!(NeighborSolicitation);
impl_icmp_message!(
    Ipv6,
    NeighborSolicitation,
    NeighborSolicitation,
    IcmpUnusedCode,
    Options<B>
);

/// An NDP Neighbor Advertisment.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct NeighborAdvertisment {
    flags_rso: u8,
    _reserved: [u8; 3],
    target_address: Ipv6Addr,
}
impl_from_bytes_as_bytes_unaligned!(NeighborAdvertisment);
impl_icmp_message!(
    Ipv6,
    NeighborAdvertisment,
    NeighborAdvertisment,
    IcmpUnusedCode,
    Options<B>
);

/// An ICMPv6 Redirect Message.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct Redirect {
    _reserved: [u8; 4],
    target_address: Ipv6Addr,
    destination_address: Ipv6Addr,
}
impl_from_bytes_as_bytes_unaligned!(Redirect);
impl_icmp_message!(Ipv6, Redirect, Redirect, IcmpUnusedCode, Options<B>);

pub mod options {
    use byteorder::{ByteOrder, NetworkEndian};
    use zerocopy::LayoutVerified;

    use crate::ip::Ipv6Addr;
    use crate::wire::util::{OptionImpl, OptionImplErr};

    create_net_enum!{
        NdpOptionType,
        SourceLinkLayerAddress: SOURCE_LINK_LAYER_ADDRESS = 1,
        TargetLinkLayerAddress: TARGET_LINK_LAYER_ADDRESS = 2,
        PrefixInformation: PREFIX_INFORMATION = 3,
        RedirectedHeader: REDIRECTED_HEADER = 4,
        Mtu: MTU = 5,
    }

    #[derive(Debug)]
    pub struct PrefixInformation<'a> {
        prefix_length: u8,
        flags_la: u8,
        valid_lifetime: &'a [u8],
        preferred_lifetime: &'a [u8],
        prefix: LayoutVerified<&'a [u8], Ipv6Addr>,
    }

    impl<'a> PrefixInformation<'a> {
        pub fn valid_lifetime(&self) -> u32 {
            NetworkEndian::read_u32(&self.valid_lifetime)
        }

        pub fn preferred_lifetime(&self) -> u32 {
            NetworkEndian::read_u32(&self.preferred_lifetime)
        }

        pub fn prefix(&self) -> &Ipv6Addr {
            &self.prefix
        }
    }

    #[allow(missing_docs)]
    #[derive(Debug)]
    pub enum NdpOption<'a> {
        SourceLinkLayerAddress(&'a [u8]),
        TargetLinkLayerAddress(&'a [u8]),
        PrefixInformation(PrefixInformation<'a>),

        RedirectedHeader { original_packet: &'a [u8] },

        MTU { mtu: &'a [u8] },
    }

    pub struct NdpOptionImpl;

    impl OptionImplErr for NdpOptionImpl {
        type Error = String;
    }

    impl<'a> OptionImpl<'a> for NdpOptionImpl {
        // For NDP options the length should be multiplied by 8.
        const OPTION_LEN_MULTIPLIER: usize = 8;

        // NDP options don't have END_OF_OPTIONS or NOP.
        const END_OF_OPTIONS: Option<u8> = None;
        const NOP: Option<u8> = None;

        type Output = NdpOption<'a>;

        fn parse(kind: u8, data: &'a [u8]) -> Result<Option<NdpOption>, String> {
            Ok(Some(match NdpOptionType::from_u8(kind) {
                Some(NdpOptionType::SourceLinkLayerAddress) => {
                    NdpOption::SourceLinkLayerAddress(data)
                }
                Some(NdpOptionType::TargetLinkLayerAddress) => {
                    NdpOption::TargetLinkLayerAddress(data)
                }
                Some(NdpOptionType::PrefixInformation) => {
                    if (data.len() < 14) {
                        // Data should always be 14 octets long
                        return Err("BadData".to_string());
                    }
                    let (prefix, _) = LayoutVerified::<_, Ipv6Addr>::new_from_prefix(&data[14..])
                        .ok_or_else(|| "No parse data".to_string())?;
                    NdpOption::PrefixInformation(PrefixInformation {
                        prefix_length: data[0],
                        flags_la: data[1],
                        valid_lifetime: &data[2..6],
                        preferred_lifetime: &data[6..10],
                        prefix,
                    })
                }
                Some(NdpOptionType::RedirectedHeader) => NdpOption::RedirectedHeader {
                    original_packet: &data[6..],
                },
                Some(NdpOptionType::Mtu) => NdpOption::MTU { mtu: &data[2..] },
                None => return Ok(None),
            }))
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    use crate::wire::icmp::{IcmpMessage, IcmpPacket};
    use crate::wire::ipv6::{Ipv6Packet, Ipv6PacketSerializer};

    #[test]
    fn parse_neighbor_solicitation() {
        use crate::wire::icmp::testdata::ndp_neighbor::*;
        let (ip, _) = Ipv6Packet::parse(SOLICITATION_IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip, hop_limit) = (ip.src_ip(), ip.dst_ip(), ip.hop_limit());
        let (icmp, _) =
            IcmpPacket::<_, _, NeighborSolicitation>::parse(ip.body(), src_ip, dst_ip).unwrap();

        assert_eq!(icmp.message().target_address.ipv6_bytes(), TARGET_ADDRESS);
        for option in icmp.ndp_options().iter() {
            match option {
                options::NdpOption::SourceLinkLayerAddress(address) => {
                    assert_eq!(address, SOURCE_LINK_LAYER_ADDRESS);
                }
                o => panic!("Found unexpected option: {:?}", o),
            }
        }
    }

    #[test]
    fn parse_neighbor_advertisment() {
        use crate::wire::icmp::testdata::ndp_neighbor::*;
        let (ip, _) = Ipv6Packet::parse(ADVERTISMENT_IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip, hop_limit) = (ip.src_ip(), ip.dst_ip(), ip.hop_limit());
        let (icmp, _) =
            IcmpPacket::<_, _, NeighborAdvertisment>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(icmp.message().target_address.ipv6_bytes(), TARGET_ADDRESS);
        assert_eq!(icmp.ndp_options().iter().count(), 0);
    }

    #[test]
    fn parse_router_advertisment() {
        use crate::wire::icmp::testdata::ndp_router::*;
        let (ip, _) = Ipv6Packet::parse(ADVERTISMENT_IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip) = (ip.src_ip(), ip.dst_ip());
        let (icmp, _) =
            IcmpPacket::<_, _, RouterAdvertisment>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(icmp.message().current_hop_limit, HOP_LIMIT);
        assert_eq!(icmp.message().router_lifetime(), LIFETIME);
        assert_eq!(icmp.message().reachable_time(), REACHABLE_TIME);
        assert_eq!(icmp.message().retransmit_timer(), RETRANS_TIMER);

        assert_eq!(icmp.ndp_options().iter().count(), 2);
        for option in icmp.ndp_options().iter() {
            match option {
                options::NdpOption::SourceLinkLayerAddress(address) => {
                    assert_eq!(address, SOURCE_LINK_LAYER_ADDRESS);
                }
                options::NdpOption::PrefixInformation(info) => {
                    assert_eq!(info.valid_lifetime(), PREFIX_INFO_VALID_LIFETIME);
                    assert_eq!(info.preferred_lifetime(), PREFIX_INFO_PREFERRED_LIFETIME);
                    assert_eq!(info.prefix().ipv6_bytes(), PREFIX_ADDRESS);
                }
                o => panic!("Found unexpected option: {:?}", o),
            }
        }
    }
}
