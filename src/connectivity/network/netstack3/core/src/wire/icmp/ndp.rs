// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Messages used for NDP (ICMPv6).

use net_types::ip::{Ipv6, Ipv6Addr};
use zerocopy::{AsBytes, ByteSlice, FromBytes, Unaligned};

use crate::wire::icmp::{IcmpIpExt, IcmpPacket, IcmpUnusedCode};
use crate::wire::{U16, U32};

/// An ICMPv6 packet with an NDP message.
#[allow(missing_docs)]
pub(crate) enum NdpPacket<B: ByteSlice> {
    RouterSolicitation(IcmpPacket<Ipv6, B, RouterSolicitation>),
    RouterAdvertisement(IcmpPacket<Ipv6, B, RouterAdvertisement>),
    NeighborSolicitation(IcmpPacket<Ipv6, B, NeighborSolicitation>),
    NeighborAdvertisement(IcmpPacket<Ipv6, B, NeighborAdvertisement>),
    Redirect(IcmpPacket<Ipv6, B, Redirect>),
}

pub(crate) type Options<B> = crate::wire::records::options::Options<B, options::NdpOptionsImpl>;
pub(crate) type OptionsSerializer<'a, I> = crate::wire::records::options::OptionsSerializer<
    'a,
    options::NdpOptionsImpl,
    options::NdpOption<'a>,
    I,
>;

/// An NDP Router Solicitation.
#[derive(Copy, Clone, Default, Debug, FromBytes, AsBytes, Unaligned, PartialEq, Eq)]
#[repr(C)]
pub(crate) struct RouterSolicitation {
    _reserved: [u8; 4],
}

impl_icmp_message!(Ipv6, RouterSolicitation, RouterSolicitation, IcmpUnusedCode, Options<B>);

/// An NDP Router Advertisement.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned, PartialEq, Eq)]
#[repr(C)]
pub(crate) struct RouterAdvertisement {
    current_hop_limit: u8,
    configuration_mo: u8,
    router_lifetime: U16,
    reachable_time: U32,
    retransmit_timer: U32,
}

impl_icmp_message!(Ipv6, RouterAdvertisement, RouterAdvertisement, IcmpUnusedCode, Options<B>);

impl RouterAdvertisement {
    /// Managed address configuration flag.
    ///
    /// When set, it indicates that addresses are available via Dynamic Host Configuration Protocol
    /// (DHCPv6).
    ///
    /// If set, the "Pther configuration" flag is redundant and can be ignored because DHCPv6 will
    /// return all available configuration information.
    const MANAGED_FLAG: u8 = 0x80;

    /// Other configuration flag.
    ///
    /// When set, it indicates that other configuration information is available via DHCPv6.
    /// Examples of such information are DNS-related information or information on other servers
    /// within the network.
    const OTHER_CONFIGURATION_FLAG: u8 = 0x40;

    pub(crate) fn new(
        current_hop_limit: u8,
        managed_flag: bool,
        other_config_flag: bool,
        router_lifetime: u16,
        reachable_time: u32,
        retransmit_timer: u32,
    ) -> Self {
        let mut configuration_mo = 0;

        if managed_flag {
            configuration_mo |= Self::MANAGED_FLAG;
        }

        if other_config_flag {
            configuration_mo |= Self::OTHER_CONFIGURATION_FLAG;
        }

        Self {
            current_hop_limit,
            configuration_mo,
            router_lifetime: U16::new(router_lifetime),
            reachable_time: U32::new(reachable_time),
            retransmit_timer: U32::new(retransmit_timer),
        }
    }

    pub(crate) fn current_hop_limit(&self) -> u8 {
        self.current_hop_limit
    }

    pub(crate) fn router_lifetime(&self) -> u16 {
        self.router_lifetime.get()
    }

    pub(crate) fn reachable_time(&self) -> u32 {
        self.reachable_time.get()
    }

    pub(crate) fn retransmit_timer(&self) -> u32 {
        self.retransmit_timer.get()
    }
}

/// An NDP Neighbor Solicitation.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned, PartialEq, Eq)]
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

/// An NDP Neighbor Advertisement.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned, PartialEq, Eq)]
#[repr(C)]
pub(crate) struct NeighborAdvertisement {
    flags_rso: u8,
    _reserved: [u8; 3],
    target_address: Ipv6Addr,
}

impl_icmp_message!(Ipv6, NeighborAdvertisement, NeighborAdvertisement, IcmpUnusedCode, Options<B>);

impl NeighborAdvertisement {
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
    /// `router_flag`, `solicited_flag`, `override_flag` and `target_address`.
    pub(crate) fn new(
        router_flag: bool,
        solicited_flag: bool,
        override_flag: bool,
        target_address: Ipv6Addr,
    ) -> Self {
        let mut flags_rso = 0;

        if router_flag {
            flags_rso |= Self::FLAG_ROUTER;
        }

        if solicited_flag {
            flags_rso |= Self::FLAG_SOLICITED;
        }

        if override_flag {
            flags_rso |= Self::FLAG_OVERRIDE;
        }

        Self { flags_rso, _reserved: [0; 3], target_address }
    }

    /// Returns the target_address of an NA message.
    pub(crate) fn target_address(&self) -> &Ipv6Addr {
        &self.target_address
    }

    /// Returns the router flag.
    pub(crate) fn router_flag(&self) -> bool {
        (self.flags_rso & Self::FLAG_ROUTER) != 0
    }

    /// Returns the solicited flag.
    pub(crate) fn solicited_flag(&self) -> bool {
        (self.flags_rso & Self::FLAG_SOLICITED) != 0
    }

    /// Returns the override flag.
    pub(crate) fn override_flag(&self) -> bool {
        (self.flags_rso & Self::FLAG_OVERRIDE) != 0
    }
}

/// An ICMPv6 Redirect Message.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned, PartialEq, Eq)]
#[repr(C)]
pub(crate) struct Redirect {
    _reserved: [u8; 4],
    target_address: Ipv6Addr,
    destination_address: Ipv6Addr,
}

impl_icmp_message!(Ipv6, Redirect, Redirect, IcmpUnusedCode, Options<B>);

pub(crate) mod options {
    use byteorder::{ByteOrder, NetworkEndian};
    use net_types::ip::{AddrSubnet, Ipv6Addr};
    use zerocopy::{AsBytes, FromBytes, LayoutVerified, Unaligned};

    use crate::wire::records::options::{OptionsImpl, OptionsImplLayout, OptionsSerializerImpl};
    use crate::wire::U32;

    /// The length of an NDP MTU option, excluding the first 2 bytes (kind and length bytes).
    ///
    /// See [RFC 4861 section 4.6.3] for more information.
    ///
    /// [RFC 4861 section 4.6.3]: https://tools.ietf.org/html/rfc4861#section-4.6.3
    const MTU_OPTION_LEN: usize = 6;

    /// Number of bytes in a Prefix Information option, excluding the kind
    /// and length bytes.
    ///
    /// See [RFC 4861 section 4.6.2] for more information.
    ///
    /// [RFC 4861 section 4.6.2]: https://tools.ietf.org/html/rfc4861#section-4.6.2
    const PREFIX_INFORMATION_OPTION_LEN: usize = 30;

    /// The on-link flag within the 4th byte in the prefix information buffer.
    ///
    /// See [RFC 4861 section 4.6.2] for more information.
    ///
    /// [RFC 4861 section 4.6.2]: https://tools.ietf.org/html/rfc4861#section-4.6.2
    const ON_LINK_FLAG: u8 = 0x80;

    /// The autonomous address configuration flag within the 4th byte in the
    /// prefix information buffer
    ///
    /// See [RFC 4861 section 4.6.2] for more information.
    ///
    /// [RFC 4861 section 4.6.2]: https://tools.ietf.org/html/rfc4861#section-4.6.2
    const AUTONOMOUS_ADDRESS_CONFIGURATION_FLAG: u8 = 0x40;

    create_net_enum! {
        pub(crate) NdpOptionType,
        SourceLinkLayerAddress: SOURCE_LINK_LAYER_ADDRESS = 1,
        TargetLinkLayerAddress: TARGET_LINK_LAYER_ADDRESS = 2,
        PrefixInformation: PREFIX_INFORMATION = 3,
        RedirectedHeader: REDIRECTED_HEADER = 4,
        Mtu: MTU = 5,
    }

    /// Prefix information that is advertised by a router in Router Advertisements.
    ///
    /// See [RFC 4861 section 4.6.2].
    ///
    /// [RFC 4861 section 4.6.2]: https://tools.ietf.org/html/rfc4861#section-4.6.2
    #[derive(Debug, FromBytes, AsBytes, Unaligned, PartialEq, Eq, Clone)]
    #[repr(C)]
    pub struct PrefixInformation {
        prefix_length: u8,
        flags_la: u8,
        valid_lifetime: U32,
        preferred_lifetime: U32,
        _reserved: [u8; 4],
        prefix: Ipv6Addr,
    }

    impl PrefixInformation {
        /// Create a new `PrefixInformation`.
        // TODO(rheacock): remove `#[cfg(test)]` when this is used.
        #[cfg(test)]
        pub(crate) fn new(
            prefix_length: u8,
            on_link_flag: bool,
            autonomous_address_configuration_flag: bool,
            valid_lifetime: u32,
            preferred_lifetime: u32,
            prefix: Ipv6Addr,
        ) -> Self {
            let mut flags_la = 0;

            if on_link_flag {
                flags_la |= ON_LINK_FLAG;
            }

            if autonomous_address_configuration_flag {
                flags_la |= AUTONOMOUS_ADDRESS_CONFIGURATION_FLAG;
            }

            Self {
                prefix_length,
                flags_la,
                valid_lifetime: U32::new(valid_lifetime),
                preferred_lifetime: U32::new(preferred_lifetime),
                _reserved: [0; 4],
                prefix,
            }
        }

        /// The number of leading bits in the prefix that are valid.
        pub(crate) fn prefix_length(&self) -> u8 {
            self.prefix_length
        }

        /// Is this prefix on the link?
        ///
        /// Returns `true` if the prefix is on-link. `false` means that
        /// no statement is made about on or off-link properties of the
        /// prefix; nodes MUST NOT conclude that an address derived
        /// from this prefix is off-link if `false`.
        pub(crate) fn on_link_flag(&self) -> bool {
            (self.flags_la & ON_LINK_FLAG) != 0
        }

        /// Can this prefix be used for stateless address configuration?
        pub(crate) fn autonomous_address_configuration_flag(&self) -> bool {
            (self.flags_la & AUTONOMOUS_ADDRESS_CONFIGURATION_FLAG) != 0
        }

        /// Get the length of time in seconds (relative to the time the
        /// packet is sent) that the prefix is valid for the purpose of
        /// on-link determination.
        ///
        /// A value of all one bits (`std::u32::MAX`) represents infinity.
        pub(crate) fn valid_lifetime(&self) -> u32 {
            self.valid_lifetime.get()
        }

        /// Get the length of time in seconds (relative to the time the
        /// packet is sent) that addresses generated from the prefix via
        /// stateless address autoconfiguration remains preferred.
        ///
        /// A value of all one bits (`std::u32::MAX`) represents infinity.
        pub(crate) fn preferred_lifetime(&self) -> u32 {
            self.preferred_lifetime.get()
        }

        /// An IPv6 address or a prefix of an IPv6 address.
        ///
        /// The number of valid leading bits in this prefix is available
        /// from [`PrefixInformation::prefix_length`];
        pub(crate) fn prefix(&self) -> &Ipv6Addr {
            &self.prefix
        }

        /// Get an [`AddrSubnet`] from this prefix.
        pub(crate) fn addr_subnet(&self) -> Option<AddrSubnet<Ipv6Addr>> {
            AddrSubnet::new(self.prefix, self.prefix_length)
        }
    }

    #[allow(missing_docs)]
    #[derive(Debug, PartialEq, Eq)]
    pub(crate) enum NdpOption<'a> {
        SourceLinkLayerAddress(&'a [u8]),
        TargetLinkLayerAddress(&'a [u8]),
        PrefixInformation(&'a PrefixInformation),

        RedirectedHeader { original_packet: &'a [u8] },

        MTU(u32),
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
                        .ok_or_else(|| "No parse data".to_string())?
                        .into_ref(),
                ),
                Some(NdpOptionType::RedirectedHeader) => {
                    NdpOption::RedirectedHeader { original_packet: &data[6..] }
                }
                Some(NdpOptionType::Mtu) => NdpOption::MTU(NetworkEndian::read_u32(&data[2..])),
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
                | NdpOption::RedirectedHeader { original_packet: data } => data.len(),
                NdpOption::MTU(_) => MTU_OPTION_LEN,
                NdpOption::PrefixInformation(_) => PREFIX_INFORMATION_OPTION_LEN,
            }
        }

        fn get_option_kind(option: &Self::Option) -> u8 {
            NdpOptionType::from(option).into()
        }

        fn serialize(buffer: &mut [u8], option: &Self::Option) {
            match option {
                NdpOption::SourceLinkLayerAddress(data)
                | NdpOption::TargetLinkLayerAddress(data)
                | NdpOption::RedirectedHeader { original_packet: data } => {
                    buffer.copy_from_slice(data);
                }
                NdpOption::PrefixInformation(pfx_info) => {
                    buffer.copy_from_slice(pfx_info.as_bytes());
                }
                NdpOption::MTU(mtu) => {
                    buffer[..2].copy_from_slice(&[0; 2]);
                    NetworkEndian::write_u32(&mut buffer[2..], *mtu);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use packet::{InnerPacketBuilder, ParseBuffer};

    use super::*;
    use crate::wire::icmp::{IcmpPacket, IcmpPacketBuilder, IcmpParseArgs};
    use crate::wire::ipv6::Ipv6Packet;
    use byteorder::{ByteOrder, NetworkEndian};
    use packet::serialize::Serializer;

    #[test]
    fn parse_serialize_mtu_option() {
        let expected_mtu = 5781;
        let options = &[options::NdpOption::MTU(expected_mtu)];
        let serialized = OptionsSerializer::<_>::new(options.iter())
            .into_serializer()
            .serialize_vec_outer()
            .unwrap();
        let mut expected = [5, 1, 0, 0, 0, 0, 0, 0];
        NetworkEndian::write_u32(&mut expected[4..], expected_mtu);
        assert_eq!(serialized.as_ref(), expected);

        let parsed = Options::parse(&expected[..]).unwrap();
        let parsed = parsed.iter().collect::<Vec<options::NdpOption>>();
        assert_eq!(parsed.len(), 1);
        if let options::NdpOption::MTU(mtu) = &parsed[0] {
            assert_eq!(*mtu, expected_mtu);
        } else {
            unreachable!("parsed option should have been an mtu option");
        }
    }

    #[test]
    fn parse_serialize_prefix_option() {
        let expected_prefix_info = options::PrefixInformation::new(
            120,
            true,
            false,
            100,
            100,
            Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 0]),
        );
        let options = &[options::NdpOption::PrefixInformation(&expected_prefix_info)];
        let serialized = OptionsSerializer::<_>::new(options.iter())
            .into_serializer()
            .serialize_vec_outer()
            .unwrap();
        let mut expected = [0; 32];
        expected[0] = 3;
        expected[1] = 4;
        (&mut expected[2..]).copy_from_slice(expected_prefix_info.as_bytes());
        assert_eq!(serialized.as_ref(), expected);

        let parsed = Options::parse(&expected[..]).unwrap();
        let parsed = parsed.iter().collect::<Vec<options::NdpOption>>();
        assert_eq!(parsed.len(), 1);
        if let options::NdpOption::PrefixInformation(prefix_info) = &parsed[0] {
            assert_eq!(expected_prefix_info, **prefix_info);
        } else {
            unreachable!("parsed option should have been a prefix information option");
        }
    }

    #[test]
    fn parse_neighbor_solicitation() {
        use crate::wire::icmp::testdata::ndp_neighbor::*;
        let mut buf = &SOLICITATION_IP_PACKET_BYTES[..];
        let ip = buf.parse::<Ipv6Packet<_>>().unwrap();
        let ipv6_builder = ip.builder();
        let (src_ip, dst_ip) = (ip.src_ip(), ip.dst_ip());
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
            .into_serializer()
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                dst_ip,
                IcmpUnusedCode,
                *icmp.message(),
            ))
            .encapsulate(ipv6_builder)
            .serialize_vec_outer()
            .unwrap()
            .as_ref()
            .to_vec();
        assert_eq!(&serialized, &SOLICITATION_IP_PACKET_BYTES)
    }

    #[test]
    fn parse_neighbor_advertisement() {
        use crate::wire::icmp::testdata::ndp_neighbor::*;
        let mut buf = &ADVERTISEMENT_IP_PACKET_BYTES[..];
        let ip = buf.parse::<Ipv6Packet<_>>().unwrap();
        let ipv6_builder = ip.builder();
        let (src_ip, dst_ip) = (ip.src_ip(), ip.dst_ip());
        let icmp = buf
            .parse_with::<_, IcmpPacket<_, _, NeighborAdvertisement>>(IcmpParseArgs::new(
                src_ip, dst_ip,
            ))
            .unwrap();
        assert_eq!(icmp.message().target_address.ipv6_bytes(), TARGET_ADDRESS);
        assert_eq!(icmp.ndp_options().iter().count(), 0);

        let serialized = []
            .into_serializer()
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                dst_ip,
                IcmpUnusedCode,
                *icmp.message(),
            ))
            .encapsulate(ipv6_builder)
            .serialize_vec_outer()
            .unwrap()
            .as_ref()
            .to_vec();
        assert_eq!(&serialized, &ADVERTISEMENT_IP_PACKET_BYTES);
    }

    #[test]
    fn parse_router_advertisement() {
        use crate::wire::icmp::testdata::ndp_router::*;
        let mut buf = &ADVERTISEMENT_IP_PACKET_BYTES[..];
        let ip = buf.parse::<Ipv6Packet<_>>().unwrap();
        let ipv6_builder = ip.builder();
        let (src_ip, dst_ip) = (ip.src_ip(), ip.dst_ip());
        let icmp = buf
            .parse_with::<_, IcmpPacket<_, _, RouterAdvertisement>>(IcmpParseArgs::new(
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
            .into_serializer()
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                dst_ip,
                IcmpUnusedCode,
                *icmp.message(),
            ))
            .encapsulate(ipv6_builder)
            .serialize_vec_outer()
            .unwrap()
            .as_ref()
            .to_vec();
        assert_eq!(&serialized, &ADVERTISEMENT_IP_PACKET_BYTES);
    }
}
