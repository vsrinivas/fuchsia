// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Messages used for NDP (ICMPv6).

#![allow(unused)] // FIXME(joshlf)

use std::marker::PhantomData;
use std::ops::Range;

use byteorder::{ByteOrder, NetworkEndian};
use packet::{EncapsulatingSerializer, Serializer};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use crate::error::ParseError;
use crate::ip::{Ipv6, Ipv6Addr};

use super::{IcmpIpExt, IcmpMessage, IcmpPacketBuilder, IcmpUnusedCode};
use crate::wire::igmp::IgmpMessage;

pub(crate) type Options<B> =
    crate::wire::util::records::options::Options<B, options::NdpOptionsImpl>;
pub(crate) type OptionsSerializer<'a, I> = crate::wire::util::records::options::OptionsSerializer<
    'a,
    options::NdpOptionsImpl,
    options::NdpOption<'a>,
    I,
>;

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

impl NeighborSolicitation {
    /// Creates a new neighbor solicitation message with the provided
    /// `target_address`.
    pub(crate) fn new(target_address: Ipv6Addr) -> Self {
        Self { _reserved: [0; 4], target_address }
    }

    /// Get the target address in neighbor solicitation message.
    pub(crate) fn target_address(&self) -> &Ipv6Addr {
        &self.target_address
    }
}

/// An NDP Neighbor Advertisment.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct NeighborAdvertisment {
    flags_rso: u8,
    _reserved: [u8; 3],
    target_address: Ipv6Addr,
}

impl_icmp_message!(Ipv6, NeighborAdvertisment, NeighborAdvertisment, IcmpUnusedCode, Options<B>);

impl NeighborAdvertisment {
    /// Router flag.
    ///
    /// When set, the R-bit indicates that the sender is a router. The R-bit is
    /// used by Neighbor Unreachability Detection to detect a router that
    /// changes to a host.
    pub(crate) const FLAG_ROUTER: u8 = 0x80;
    /// Solicited flag.
    ///
    /// When set, the S-bit indicates that the advertisement was sent in
    /// response to a Neighbor Solicitation from the Destination address. The
    /// S-bit is used as a reachability confirmation for Neighbor Unreachability
    /// Detection.  It MUST NOT be set in multicast advertisements or in
    /// unsolicited unicast advertisements.
    pub(crate) const FLAG_SOLICITED: u8 = 0x40;
    /// Override flag.
    ///
    /// When set, the O-bit indicates that the advertisement should override an
    /// existing cache entry and update the cached link-layer address. When it
    /// is not set the advertisement will not update a cached link-layer address
    /// though it will update an existing Neighbor Cache entry for which no
    /// link-layer address is known.  It SHOULD NOT be set in solicited
    /// advertisements for anycast addresses and in solicited proxy
    /// advertisements. It SHOULD be set in other solicited advertisements and
    /// in unsolicited advertisements.
    pub(crate) const FLAG_OVERRIDE: u8 = 0x20;

    /// Creates a new neighbor advertisement message with the provided
    /// `flags_rso` and `target_address`.
    pub(crate) fn new(flags_rso: u8, target_address: Ipv6Addr) -> Self {
        Self { flags_rso, _reserved: [0; 3], target_address }
    }
}

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
    use zerocopy::{AsBytes, FromBytes, LayoutVerified, Unaligned};

    use crate::ip::Ipv6Addr;
    use crate::wire::util::records::options::{
        OptionsImpl, OptionsImplLayout, OptionsSerializerImpl,
    };

    create_net_enum! {
        NdpOptionType,
        SourceLinkLayerAddress: SOURCE_LINK_LAYER_ADDRESS = 1,
        TargetLinkLayerAddress: TARGET_LINK_LAYER_ADDRESS = 2,
        PrefixInformation: PREFIX_INFORMATION = 3,
        RedirectedHeader: REDIRECTED_HEADER = 4,
        Mtu: MTU = 5,
    }

    #[derive(Debug, FromBytes, AsBytes, Unaligned)]
    #[repr(C)]
    pub(crate) struct PrefixInformation {
        prefix_length: u8,
        flags_la: u8,
        valid_lifetime: [u8; 4],
        preferred_lifetime: [u8; 4],
        _reserved: [u8; 4],
        prefix: Ipv6Addr,
    }

    impl PrefixInformation {
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
        PrefixInformation(LayoutVerified<&'a [u8], PrefixInformation>),

        RedirectedHeader { original_packet: &'a [u8] },

        MTU { mtu: &'a [u8] },
    }

    impl<'a> From<&NdpOption<'a>> for NdpOptionType {
        fn from(v: &NdpOption<'a>) -> Self {
            match v {
                NdpOption::SourceLinkLayerAddress(_) => NdpOptionType::SourceLinkLayerAddress,
                NdpOption::TargetLinkLayerAddress(_) => NdpOptionType::TargetLinkLayerAddress,
                NdpOption::PrefixInformation(_) => NdpOptionType::PrefixInformation,
                NdpOption::RedirectedHeader { .. } => NdpOptionType::RedirectedHeader,
                NdpOption::MTU { .. } => NdpOptionType::Mtu,
            }
        }
    }

    #[derive(Debug)]
    pub(crate) struct NdpOptionsImpl;

    impl OptionsImplLayout for NdpOptionsImpl {
        type Error = String;

        // For NDP options the length should be multiplied by 8.
        const OPTION_LEN_MULTIPLIER: usize = 8;

        // NDP options don't have END_OF_OPTIONS or NOP.
        const END_OF_OPTIONS: Option<u8> = None;
        const NOP: Option<u8> = None;
    }

    impl<'a> OptionsImpl<'a> for NdpOptionsImpl {
        type Option = NdpOption<'a>;

        fn parse(kind: u8, data: &'a [u8]) -> Result<Option<NdpOption>, String> {
            Ok(Some(match NdpOptionType::from_u8(kind) {
                Some(NdpOptionType::SourceLinkLayerAddress) => {
                    NdpOption::SourceLinkLayerAddress(data)
                }
                Some(NdpOptionType::TargetLinkLayerAddress) => {
                    NdpOption::TargetLinkLayerAddress(data)
                }
                Some(NdpOptionType::PrefixInformation) => NdpOption::PrefixInformation(
                    LayoutVerified::<_, PrefixInformation>::new(data)
                        .ok_or_else(|| "No parse data".to_string())?,
                ),
                Some(NdpOptionType::RedirectedHeader) => {
                    NdpOption::RedirectedHeader { original_packet: &data[6..] }
                }
                Some(NdpOptionType::Mtu) => NdpOption::MTU { mtu: &data[2..] },
                None => return Ok(None),
            }))
        }
    }

    impl<'a> OptionsSerializerImpl<'a> for NdpOptionsImpl {
        type Option = NdpOption<'a>;

        fn get_option_length(option: &Self::Option) -> usize {
            match option {
                NdpOption::SourceLinkLayerAddress(data)
                | NdpOption::TargetLinkLayerAddress(data)
                | NdpOption::RedirectedHeader { original_packet: data }
                | NdpOption::MTU { mtu: data } => data.len(),
                NdpOption::PrefixInformation(pfx_info) => pfx_info.bytes().len(),
            }
        }

        fn get_option_kind(option: &Self::Option) -> u8 {
            NdpOptionType::from(option).into()
        }

        fn serialize(buffer: &mut [u8], option: &Self::Option) {
            let bytes = match option {
                NdpOption::SourceLinkLayerAddress(data)
                | NdpOption::TargetLinkLayerAddress(data)
                | NdpOption::RedirectedHeader { original_packet: data }
                | NdpOption::MTU { mtu: data } => data,
                NdpOption::PrefixInformation(pfx_info) => pfx_info.bytes(),
            };
            buffer.copy_from_slice(bytes);
        }
    }
}

#[cfg(test)]
mod tests {
    use packet::{ParsablePacket, ParseBuffer};

    use super::*;
    use crate::ip;
    use crate::wire::icmp::{IcmpMessage, IcmpPacket, IcmpPacketBuilder, IcmpParseArgs};
    use crate::wire::ipv6::{Ipv6Packet, Ipv6PacketBuilder};
    use packet::serialize::Serializer;

    #[test]
    fn parse_neighbor_solicitation() {
        use crate::wire::icmp::testdata::ndp_neighbor::*;
        let mut buf = &SOLICITATION_IP_PACKET_BYTES[..];
        let ip = buf.parse::<Ipv6Packet<_>>().unwrap();
        let ip_builder = ip.builder();
        let (src_ip, dst_ip, hop_limit) = (ip.src_ip(), ip.dst_ip(), ip.hop_limit());
        let icmp = buf
            .parse_with::<_, IcmpPacket<_, _, NeighborSolicitation>>(IcmpParseArgs::new(
                src_ip, dst_ip,
            ))
            .unwrap();

        assert_eq!(icmp.message().target_address.ipv6_bytes(), TARGET_ADDRESS);
        let collected = icmp.ndp_options().iter().collect::<Vec<options::NdpOption>>();
        for option in collected.iter() {
            match option {
                options::NdpOption::SourceLinkLayerAddress(address) => {
                    assert_eq!(address, &SOURCE_LINK_LAYER_ADDRESS);
                }
                o => panic!("Found unexpected option: {:?}", o),
            }
        }
        let serialized = OptionsSerializer::<_>::new(collected.iter())
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                dst_ip,
                IcmpUnusedCode,
                *icmp.message(),
            ))
            .encapsulate(ip_builder)
            .serialize_outer()
            .unwrap()
            .as_ref()
            .to_vec();
        assert_eq!(&serialized, &SOLICITATION_IP_PACKET_BYTES)
    }

    #[test]
    fn parse_neighbor_advertisment() {
        use crate::wire::icmp::testdata::ndp_neighbor::*;
        let mut buf = &ADVERTISMENT_IP_PACKET_BYTES[..];
        let ip = buf.parse::<Ipv6Packet<_>>().unwrap();
        let ip_builder = ip.builder();
        let (src_ip, dst_ip, hop_limit) = (ip.src_ip(), ip.dst_ip(), ip.hop_limit());
        let icmp = buf
            .parse_with::<_, IcmpPacket<_, _, NeighborAdvertisment>>(IcmpParseArgs::new(
                src_ip, dst_ip,
            ))
            .unwrap();
        assert_eq!(icmp.message().target_address.ipv6_bytes(), TARGET_ADDRESS);
        assert_eq!(icmp.ndp_options().iter().count(), 0);

        let serialized = []
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                dst_ip,
                IcmpUnusedCode,
                *icmp.message(),
            ))
            .encapsulate(ip_builder)
            .serialize_outer()
            .unwrap()
            .as_ref()
            .to_vec();
        assert_eq!(&serialized, &ADVERTISMENT_IP_PACKET_BYTES);
    }

    #[test]
    fn parse_router_advertisment() {
        use crate::wire::icmp::testdata::ndp_router::*;
        let mut buf = &ADVERTISMENT_IP_PACKET_BYTES[..];
        let ip = buf.parse::<Ipv6Packet<_>>().unwrap();
        let ip_builder = ip.builder();
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

        let collected = icmp.ndp_options().iter().collect::<Vec<options::NdpOption>>();
        for option in collected.iter() {
            match option {
                options::NdpOption::SourceLinkLayerAddress(address) => {
                    assert_eq!(address, &SOURCE_LINK_LAYER_ADDRESS);
                }
                options::NdpOption::PrefixInformation(info) => {
                    assert_eq!(info.valid_lifetime(), PREFIX_INFO_VALID_LIFETIME);
                    assert_eq!(info.preferred_lifetime(), PREFIX_INFO_PREFERRED_LIFETIME);
                    assert_eq!(info.prefix().ipv6_bytes(), PREFIX_ADDRESS);
                }
                o => panic!("Found unexpected option: {:?}", o),
            }
        }

        let serialized = OptionsSerializer::<_>::new(collected.iter())
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                dst_ip,
                IcmpUnusedCode,
                *icmp.message(),
            ))
            .encapsulate(ip_builder)
            .serialize_outer()
            .unwrap()
            .as_ref()
            .to_vec();
        assert_eq!(&serialized, &ADVERTISMENT_IP_PACKET_BYTES);
    }
}
