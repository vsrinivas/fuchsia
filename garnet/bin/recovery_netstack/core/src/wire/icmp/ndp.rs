// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Messages used for NDP (ICMPv6).

#![allow(unused)] // FIXME(joshlf)

use std::marker::PhantomData;
use std::ops::Range;

use byteorder::{ByteOrder, NetworkEndian};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use crate::error::ParseError;
use crate::ip::{Ipv6, Ipv6Addr};
use crate::wire::util;

use super::{IcmpIpExt, IcmpUnusedCode};

pub(crate) type Options<B> = util::Options<B, options::NdpOptionImpl>;

/// An NDP Router Solicitation.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct RouterSolicitation {
    _reserved: [u8; 4],
}

impl_icmp_message!(Ipv6, RouterSolicitation, RouterSolicitation, IcmpUnusedCode, Options<B>);

/// An NDP Router Advertisment.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct RouterAdvertisment {
    current_hop_limit: u8,
    configuration_mo: u8,
    router_lifetime: [u8; 2],
    reachable_time: [u8; 4],
    retransmit_timer: [u8; 4],
}

impl_icmp_message!(Ipv6, RouterAdvertisment, RouterAdvertisment, IcmpUnusedCode, Options<B>);

impl RouterAdvertisment {
    pub(crate) fn router_lifetime(&self) -> u16 {
        NetworkEndian::read_u16(&self.router_lifetime)
    }

    pub(crate) fn reachable_time(&self) -> u32 {
        NetworkEndian::read_u32(&self.reachable_time)
    }

    pub(crate) fn retransmit_timer(&self) -> u32 {
        NetworkEndian::read_u32(&self.retransmit_timer)
    }
}

/// An NDP Neighbor Solicitation.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct NeighborSolicitation {
    _reserved: [u8; 4],
    target_address: Ipv6Addr,
}

impl_icmp_message!(Ipv6, NeighborSolicitation, NeighborSolicitation, IcmpUnusedCode, Options<B>);

/// An NDP Neighbor Advertisment.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct NeighborAdvertisment {
    flags_rso: u8,
    _reserved: [u8; 3],
    target_address: Ipv6Addr,
}

impl_icmp_message!(Ipv6, NeighborAdvertisment, NeighborAdvertisment, IcmpUnusedCode, Options<B>);

/// An ICMPv6 Redirect Message.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct Redirect {
    _reserved: [u8; 4],
    target_address: Ipv6Addr,
    destination_address: Ipv6Addr,
}

impl_icmp_message!(Ipv6, Redirect, Redirect, IcmpUnusedCode, Options<B>);

pub(crate) mod options {
    use byteorder::{ByteOrder, NetworkEndian};
    use zerocopy::LayoutVerified;

    use crate::ip::Ipv6Addr;
    use crate::wire::util::{OptionImpl, OptionImplErr};

    create_net_enum! {
        NdpOptionType,
        SourceLinkLayerAddress: SOURCE_LINK_LAYER_ADDRESS = 1,
        TargetLinkLayerAddress: TARGET_LINK_LAYER_ADDRESS = 2,
        PrefixInformation: PREFIX_INFORMATION = 3,
        RedirectedHeader: REDIRECTED_HEADER = 4,
        Mtu: MTU = 5,
    }

    #[derive(Debug)]
    pub(crate) struct PrefixInformation<'a> {
        prefix_length: u8,
        flags_la: u8,
        valid_lifetime: &'a [u8],
        preferred_lifetime: &'a [u8],
        prefix: LayoutVerified<&'a [u8], Ipv6Addr>,
    }

    impl<'a> PrefixInformation<'a> {
        pub(crate) fn valid_lifetime(&self) -> u32 {
            NetworkEndian::read_u32(&self.valid_lifetime)
        }

        pub(crate) fn preferred_lifetime(&self) -> u32 {
            NetworkEndian::read_u32(&self.preferred_lifetime)
        }

        pub(crate) fn prefix(&self) -> &Ipv6Addr {
            &self.prefix
        }
    }

    #[allow(missing_docs)]
    #[derive(Debug)]
    pub(crate) enum NdpOption<'a> {
        SourceLinkLayerAddress(&'a [u8]),
        TargetLinkLayerAddress(&'a [u8]),
        PrefixInformation(PrefixInformation<'a>),

        RedirectedHeader { original_packet: &'a [u8] },

        MTU { mtu: &'a [u8] },
    }

    #[derive(Debug)]
    pub(crate) struct NdpOptionImpl;

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
                    if data.len() < 14 {
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
                Some(NdpOptionType::RedirectedHeader) => {
                    NdpOption::RedirectedHeader { original_packet: &data[6..] }
                }
                Some(NdpOptionType::Mtu) => NdpOption::MTU { mtu: &data[2..] },
                None => return Ok(None),
            }))
        }
    }
}

#[cfg(test)]
mod tests {
    use packet::{ParsablePacket, ParseBuffer};

    use super::*;
    use crate::wire::icmp::{IcmpMessage, IcmpPacket, IcmpParseArgs};
    use crate::wire::ipv6::{Ipv6Packet, Ipv6PacketBuilder};

    #[test]
    fn parse_neighbor_solicitation() {
        use crate::wire::icmp::testdata::ndp_neighbor::*;
        let mut buf = &SOLICITATION_IP_PACKET_BYTES[..];
        let ip = buf.parse::<Ipv6Packet<_>>().unwrap();
        let (src_ip, dst_ip, hop_limit) = (ip.src_ip(), ip.dst_ip(), ip.hop_limit());
        let icmp = buf
            .parse_with::<_, IcmpPacket<_, _, NeighborSolicitation>>(IcmpParseArgs::new(
                src_ip, dst_ip,
            ))
            .unwrap();

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
        let mut buf = &ADVERTISMENT_IP_PACKET_BYTES[..];
        let ip = buf.parse::<Ipv6Packet<_>>().unwrap();
        let (src_ip, dst_ip, hop_limit) = (ip.src_ip(), ip.dst_ip(), ip.hop_limit());
        let icmp = buf
            .parse_with::<_, IcmpPacket<_, _, NeighborAdvertisment>>(IcmpParseArgs::new(
                src_ip, dst_ip,
            ))
            .unwrap();
        assert_eq!(icmp.message().target_address.ipv6_bytes(), TARGET_ADDRESS);
        assert_eq!(icmp.ndp_options().iter().count(), 0);
    }

    #[test]
    fn parse_router_advertisment() {
        use crate::wire::icmp::testdata::ndp_router::*;
        let mut buf = &ADVERTISMENT_IP_PACKET_BYTES[..];
        let ip = buf.parse::<Ipv6Packet<_>>().unwrap();
        let (src_ip, dst_ip) = (ip.src_ip(), ip.dst_ip());
        let icmp = buf
            .parse_with::<_, IcmpPacket<_, _, RouterAdvertisment>>(IcmpParseArgs::new(
                src_ip, dst_ip,
            ))
            .unwrap();
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
