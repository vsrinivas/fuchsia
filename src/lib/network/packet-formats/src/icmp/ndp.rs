// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Messages used for NDP (ICMPv6).

use core::num::NonZeroU8;
use core::time::Duration;

use net_types::ip::{Ipv6, Ipv6Addr};
use zerocopy::{AsBytes, ByteSlice, FromBytes, Unaligned};

use crate::icmp::{IcmpIpExt, IcmpPacket, IcmpUnusedCode};
use crate::utils::NonZeroDuration;
use crate::{U16, U32};

/// An ICMPv6 packet with an NDP message.
#[allow(missing_docs)]
#[derive(Debug)]
pub enum NdpPacket<B: ByteSlice> {
    RouterSolicitation(IcmpPacket<Ipv6, B, RouterSolicitation>),
    RouterAdvertisement(IcmpPacket<Ipv6, B, RouterAdvertisement>),
    NeighborSolicitation(IcmpPacket<Ipv6, B, NeighborSolicitation>),
    NeighborAdvertisement(IcmpPacket<Ipv6, B, NeighborAdvertisement>),
    Redirect(IcmpPacket<Ipv6, B, Redirect>),
}

/// A records parser for NDP options.
///
/// See [`Options`] for more details.
///
/// [`Options`]: packet::records::options::Options
pub type Options<B> = packet::records::options::Options<B, options::NdpOptionsImpl>;

/// A records serializer for NDP options.
///
/// See [`OptionsSerializer`] for more details.
///
/// [`OptionsSerializer`]: packet::records::options::OptionsSerializer
pub type OptionsSerializer<'a, I> = packet::records::options::OptionsSerializer<
    'a,
    options::NdpOptionsImpl,
    options::NdpOption<'a>,
    I,
>;

/// An NDP Router Solicitation.
#[derive(Copy, Clone, Default, Debug, FromBytes, AsBytes, Unaligned, PartialEq, Eq)]
#[repr(C)]
pub struct RouterSolicitation {
    _reserved: [u8; 4],
}

impl_icmp_message!(Ipv6, RouterSolicitation, RouterSolicitation, IcmpUnusedCode, Options<B>);

/// An NDP Router Advertisement.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned, PartialEq, Eq)]
#[repr(C)]
pub struct RouterAdvertisement {
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

    /// Creates a new Router Advertisement with the specified field values.
    pub fn new(
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

    /// Returns the current hop limit field.
    ///
    /// A value of `None` means unspecified by the source of the Router Advertisement.
    pub fn current_hop_limit(&self) -> Option<NonZeroU8> {
        NonZeroU8::new(self.current_hop_limit)
    }

    /// Returns the router lifetime.
    ///
    /// A value of `None` indicates that the router is not a default router and SHOULD
    /// NOT appear in the default router list.
    pub fn router_lifetime(&self) -> Option<NonZeroDuration> {
        // As per RFC 4861 section 4.2, the Router Lifetime field is held in units
        // of seconds.
        NonZeroDuration::new(Duration::from_secs(self.router_lifetime.get().into()))
    }

    /// Returns the reachable time.
    ///
    /// A value of `None` means unspecified by the source of the Router Advertisement.
    pub fn reachable_time(&self) -> Option<NonZeroDuration> {
        // As per RFC 4861 section 4.2, the Reachable Time field is held in units
        // of milliseconds.
        NonZeroDuration::new(Duration::from_millis(self.reachable_time.get().into()))
    }

    /// Returns the retransmit timer.
    ///
    /// A value of `None` means unspecified by the source of the Router Advertisement.
    pub fn retransmit_timer(&self) -> Option<NonZeroDuration> {
        // As per RFC 4861 section 4.2, the Retransmit Timer field is held in units
        // of milliseconds
        NonZeroDuration::new(Duration::from_millis(self.retransmit_timer.get().into()))
    }
}

/// An NDP Neighbor Solicitation.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned, PartialEq, Eq)]
#[repr(C)]
pub struct NeighborSolicitation {
    _reserved: [u8; 4],
    target_address: Ipv6Addr,
}

impl_icmp_message!(Ipv6, NeighborSolicitation, NeighborSolicitation, IcmpUnusedCode, Options<B>);

impl NeighborSolicitation {
    /// Creates a new neighbor solicitation message with the provided
    /// `target_address`.
    pub fn new(target_address: Ipv6Addr) -> Self {
        Self { _reserved: [0; 4], target_address }
    }

    /// Get the target address in neighbor solicitation message.
    pub fn target_address(&self) -> &Ipv6Addr {
        &self.target_address
    }
}

/// An NDP Neighbor Advertisement.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned, PartialEq, Eq)]
#[repr(C)]
pub struct NeighborAdvertisement {
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
    const FLAG_ROUTER: u8 = 0x80;

    /// Solicited flag.
    ///
    /// When set, the S-bit indicates that the advertisement was sent in
    /// response to a Neighbor Solicitation from the Destination address. The
    /// S-bit is used as a reachability confirmation for Neighbor Unreachability
    /// Detection.  It MUST NOT be set in multicast advertisements or in
    /// unsolicited unicast advertisements.
    const FLAG_SOLICITED: u8 = 0x40;

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
    const FLAG_OVERRIDE: u8 = 0x20;

    /// Creates a new neighbor advertisement message with the provided
    /// `router_flag`, `solicited_flag`, `override_flag` and `target_address`.
    pub fn new(
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
    pub fn target_address(&self) -> &Ipv6Addr {
        &self.target_address
    }

    /// Returns the router flag.
    pub fn router_flag(&self) -> bool {
        (self.flags_rso & Self::FLAG_ROUTER) != 0
    }

    /// Returns the solicited flag.
    pub fn solicited_flag(&self) -> bool {
        (self.flags_rso & Self::FLAG_SOLICITED) != 0
    }

    /// Returns the override flag.
    pub fn override_flag(&self) -> bool {
        (self.flags_rso & Self::FLAG_OVERRIDE) != 0
    }
}

/// An ICMPv6 Redirect Message.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned, PartialEq, Eq)]
#[repr(C)]
pub struct Redirect {
    _reserved: [u8; 4],
    target_address: Ipv6Addr,
    destination_address: Ipv6Addr,
}

impl_icmp_message!(Ipv6, Redirect, Redirect, IcmpUnusedCode, Options<B>);

/// Parsing and serialization of NDP options.
pub mod options {
    use core::convert::TryFrom;
    use core::time::Duration;

    use byteorder::{ByteOrder, NetworkEndian};
    use net_types::ip::{AddrSubnet, AddrSubnetError, Ipv6Addr};
    use net_types::UnicastAddress;
    use packet::records::options::{OptionsImpl, OptionsImplLayout, OptionsSerializerImpl};
    use zerocopy::{AsBytes, FromBytes, LayoutVerified, Unaligned};

    use crate::utils::NonZeroDuration;
    use crate::U32;

    /// A value representing an infinite lifetime for various NDP options' lifetime
    /// fields.
    pub const INFINITE_LIFETIME: NonZeroDuration =
        unsafe { NonZeroDuration::new_unchecked(Duration::from_secs(core::u32::MAX as u64)) };

    /// The number of reserved bytes immediately following the kind and length
    /// bytes in a Redirected Header option.
    ///
    /// See [RFC 4861 section 4.6.3] for more information.
    ///
    /// [RFC 4861 section 4.6.3]: https://tools.ietf.org/html/rfc4861#section-4.6.3
    const REDIRECTED_HEADER_OPTION_RESERVED_BYTES_LENGTH: usize = 6;

    /// The length of an NDP MTU option, excluding the first 2 bytes (kind and length bytes).
    ///
    /// See [RFC 4861 section 4.6.3] for more information.
    ///
    /// [RFC 4861 section 4.6.3]: https://tools.ietf.org/html/rfc4861#section-4.6.3
    const MTU_OPTION_LENGTH: usize = 6;

    /// The number of reserved bytes immediately following the kind and length
    /// bytes in an MTU option.
    ///
    /// See [RFC 4861 section 4.6.4] for more information.
    ///
    /// [RFC 4861 section 4.6.4]: https://tools.ietf.org/html/rfc4861#section-4.6.4
    const MTU_OPTION_RESERVED_BYTES_LENGTH: usize = 2;

    /// Minimum number of bytes in a Recursive DNS Server option, excluding the
    /// kind and length bytes.
    ///
    /// This guarantees that a valid Recurisve DNS Server option holds at least
    /// 1 address.
    ///
    /// See [RFC 8106 section 5.3.1] for more information.
    ///
    /// [RFC 8106 section 5.3.1]: https://tools.ietf.org/html/rfc8106#section-5.1
    const MIN_RECURSIVE_DNS_SERVER_OPTION_LENGTH: usize = 22;

    /// The number of reserved bytes immediately following the kind and length
    /// bytes in a Recursive DNS Server option.
    ///
    /// See [RFC 8106 section 5.3.1] for more information.
    ///
    /// [RFC 8106 section 5.3.1]: https://tools.ietf.org/html/rfc8106#section-5.1
    const RECURSIVE_DNS_SERVER_OPTION_RESERVED_BYTES_LENGTH: usize = 2;

    /// Recursive DNS Server that is advertised by a router in Router Advertisements.
    ///
    /// See [RFC 8106 section 5.1].
    ///
    /// [RFC 8106 section 5.1]: https://tools.ietf.org/html/rfc8106#section-5.1
    #[derive(Debug, PartialEq, Eq, Clone)]
    pub struct RecursiveDnsServer<'a> {
        lifetime: u32,
        addresses: &'a [Ipv6Addr],
    }

    impl<'a> RecursiveDnsServer<'a> {
        /// Returns a new `RecursiveDnsServer`.
        pub fn new(lifetime: u32, addresses: &'a [Ipv6Addr]) -> RecursiveDnsServer<'a> {
            RecursiveDnsServer { lifetime, addresses }
        }

        /// Returns the length of time (relative to the time the packet is sent) that
        /// the DNS servers are valid for name resolution.
        ///
        /// A value of [`INFINITE_LIFETIME`] represents infinity; a value of `None`
        /// means that the servers MUST no longer be used.
        pub fn lifetime(&self) -> Option<NonZeroDuration> {
            NonZeroDuration::new(Duration::from_secs(self.lifetime.into()))
        }

        /// Returns the recursive DNS server addresses.
        pub fn iter_addresses(&self) -> &'a [Ipv6Addr] {
            self.addresses
        }
    }

    /// Number of bytes in a Prefix Information option, excluding the kind
    /// and length bytes.
    ///
    /// See [RFC 4861 section 4.6.2] for more information.
    ///
    /// [RFC 4861 section 4.6.2]: https://tools.ietf.org/html/rfc4861#section-4.6.2
    const PREFIX_INFORMATION_OPTION_LENGTH: usize = 30;

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

        /// Create a new `PrefixInformation`.
        pub fn new(
            prefix_length: u8,
            on_link_flag: bool,
            autonomous_address_configuration_flag: bool,
            valid_lifetime: u32,
            preferred_lifetime: u32,
            prefix: Ipv6Addr,
        ) -> Self {
            let mut flags_la = 0;

            if on_link_flag {
                flags_la |= Self::ON_LINK_FLAG;
            }

            if autonomous_address_configuration_flag {
                flags_la |= Self::AUTONOMOUS_ADDRESS_CONFIGURATION_FLAG;
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
        pub fn prefix_length(&self) -> u8 {
            self.prefix_length
        }

        /// Is this prefix on the link?
        ///
        /// Returns `true` if the prefix is on-link. `false` means that
        /// no statement is made about on or off-link properties of the
        /// prefix; nodes MUST NOT conclude that an address derived
        /// from this prefix is off-link if `false`.
        pub fn on_link_flag(&self) -> bool {
            (self.flags_la & Self::ON_LINK_FLAG) != 0
        }

        /// Can this prefix be used for stateless address configuration?
        pub fn autonomous_address_configuration_flag(&self) -> bool {
            (self.flags_la & Self::AUTONOMOUS_ADDRESS_CONFIGURATION_FLAG) != 0
        }

        /// Get the length of time (relative to the time the packet is sent) that
        /// the prefix is valid for the purpose of on-link determination and SLAAC.
        ///
        /// A value of [`INFINITE_LIFETIME`] represents infinity; a value of `None`
        /// means that the prefix must no longer be used for on-link determination.
        pub fn valid_lifetime(&self) -> Option<NonZeroDuration> {
            NonZeroDuration::new(Duration::from_secs(self.valid_lifetime.get().into()))
        }

        /// Get the length of time (relative to the time the packet is sent) that
        /// addresses generated from the prefix via SLAAC remains preferred.
        ///
        /// A value of [`INFINITE_LIFETIME`] represents infinity; a value of `None`
        /// means that the prefix should be immediately deprecated.
        pub fn preferred_lifetime(&self) -> Option<NonZeroDuration> {
            NonZeroDuration::new(Duration::from_secs(self.preferred_lifetime.get().into()))
        }

        /// An IPv6 address or a prefix of an IPv6 address.
        ///
        /// The number of valid leading bits in this prefix is available
        /// from [`PrefixInformation::prefix_length`];
        pub fn prefix(&self) -> &Ipv6Addr {
            &self.prefix
        }

        /// Get an [`AddrSubnet`] from this prefix.
        pub fn addr_subnet(&self) -> Result<AddrSubnet<Ipv6Addr>, AddrSubnetError> {
            AddrSubnet::new(self.prefix, self.prefix_length)
        }
    }

    create_protocol_enum!(
        /// The types of NDP options that may be found in NDP messages.
        #[allow(missing_docs)]
        pub enum NdpOptionType: u8 {
            SourceLinkLayerAddress, 1, "Source Link-Layer Address";
            TargetLinkLayerAddress, 2, "Target Link-Layer Address";
            PrefixInformation, 3, "Prefix Information";
            RedirectedHeader, 4, "Redirected Header";
            Mtu, 5, "MTU";
            RecursiveDnsServer, 25, "Recursive DNS Server";
        }
    );

    /// NDP options that may be found in NDP messages.
    #[allow(missing_docs)]
    #[derive(Debug, PartialEq, Eq)]
    pub enum NdpOption<'a> {
        SourceLinkLayerAddress(&'a [u8]),
        TargetLinkLayerAddress(&'a [u8]),
        PrefixInformation(&'a PrefixInformation),

        RedirectedHeader { original_packet: &'a [u8] },

        MTU(u32),

        RecursiveDnsServer(RecursiveDnsServer<'a>),
    }

    impl<'a> From<&NdpOption<'a>> for NdpOptionType {
        fn from(v: &NdpOption<'a>) -> Self {
            match v {
                NdpOption::SourceLinkLayerAddress(_) => NdpOptionType::SourceLinkLayerAddress,
                NdpOption::TargetLinkLayerAddress(_) => NdpOptionType::TargetLinkLayerAddress,
                NdpOption::PrefixInformation(_) => NdpOptionType::PrefixInformation,
                NdpOption::RedirectedHeader { .. } => NdpOptionType::RedirectedHeader,
                NdpOption::MTU { .. } => NdpOptionType::Mtu,
                NdpOption::RecursiveDnsServer(_) => NdpOptionType::RecursiveDnsServer,
            }
        }
    }

    /// An implementation of [`OptionsImpl`] for NDP options.
    #[derive(Debug)]
    pub struct NdpOptionsImpl;

    impl OptionsImplLayout for NdpOptionsImpl {
        // TODO(fxbug.dev/52288): Return more verbose logs on parsing errors.
        type Error = ();

        // For NDP options the length should be multiplied by 8.
        const OPTION_LEN_MULTIPLIER: usize = 8;

        // NDP options don't have END_OF_OPTIONS or NOP.
        const END_OF_OPTIONS: Option<u8> = None;
        const NOP: Option<u8> = None;
    }

    impl<'a> OptionsImpl<'a> for NdpOptionsImpl {
        type Option = NdpOption<'a>;

        fn parse(kind: u8, data: &'a [u8]) -> Result<Option<NdpOption<'a>>, ()> {
            NdpOptionType::try_from(kind)
                .ok()
                .map(|typ| {
                    Ok(match typ {
                        NdpOptionType::SourceLinkLayerAddress => {
                            NdpOption::SourceLinkLayerAddress(data)
                        }
                        NdpOptionType::TargetLinkLayerAddress => {
                            NdpOption::TargetLinkLayerAddress(data)
                        }
                        NdpOptionType::PrefixInformation => {
                            let data =
                                LayoutVerified::<_, PrefixInformation>::new(data).ok_or(())?;
                            NdpOption::PrefixInformation(data.into_ref())
                        }
                        NdpOptionType::RedirectedHeader => NdpOption::RedirectedHeader {
                            original_packet: &data
                                [REDIRECTED_HEADER_OPTION_RESERVED_BYTES_LENGTH..],
                        },
                        NdpOptionType::Mtu => NdpOption::MTU(NetworkEndian::read_u32(
                            &data[MTU_OPTION_RESERVED_BYTES_LENGTH..],
                        )),
                        NdpOptionType::RecursiveDnsServer => {
                            if data.len() < MIN_RECURSIVE_DNS_SERVER_OPTION_LENGTH {
                                return Err(());
                            }

                            // Skip the reserved bytes which immediately follow the kind and length
                            // bytes.
                            let (_, data) =
                                data.split_at(RECURSIVE_DNS_SERVER_OPTION_RESERVED_BYTES_LENGTH);

                            // As per RFC 8106 section 5.1, the 32 bit lifetime field immediately
                            // follows the reserved field.
                            let (lifetime, data) =
                                LayoutVerified::<_, U32>::new_from_prefix(data).ok_or(())?;

                            // As per RFC 8106 section 5.1, the list of addresses immediately
                            // follows the lifetime field.
                            let addresses =
                                LayoutVerified::<_, [Ipv6Addr]>::new_slice_unaligned(data)
                                    .ok_or(())?
                                    .into_slice();

                            // As per RFC 8106 section 5.3.1, the addresses should all be unicast.
                            if !addresses.iter().all(UnicastAddress::is_unicast) {
                                return Err(());
                            }

                            NdpOption::RecursiveDnsServer(RecursiveDnsServer::new(
                                lifetime.get(),
                                addresses,
                            ))
                        }
                    })
                })
                .transpose()
        }
    }

    impl<'a> OptionsSerializerImpl<'a> for NdpOptionsImpl {
        type Option = NdpOption<'a>;

        fn option_length(option: &Self::Option) -> usize {
            match option {
                NdpOption::SourceLinkLayerAddress(data)
                | NdpOption::TargetLinkLayerAddress(data) => data.len(),
                NdpOption::PrefixInformation(_) => PREFIX_INFORMATION_OPTION_LENGTH,
                NdpOption::RedirectedHeader { original_packet } => {
                    REDIRECTED_HEADER_OPTION_RESERVED_BYTES_LENGTH + original_packet.len()
                }
                NdpOption::MTU(_) => MTU_OPTION_LENGTH,
                NdpOption::RecursiveDnsServer(RecursiveDnsServer { lifetime, addresses }) => {
                    RECURSIVE_DNS_SERVER_OPTION_RESERVED_BYTES_LENGTH
                        + core::mem::size_of_val(lifetime)
                        + core::mem::size_of_val(*addresses)
                }
            }
        }

        fn option_kind(option: &Self::Option) -> u8 {
            NdpOptionType::from(option).into()
        }

        fn serialize(buffer: &mut [u8], option: &Self::Option) {
            match option {
                NdpOption::SourceLinkLayerAddress(data)
                | NdpOption::TargetLinkLayerAddress(data) => buffer.copy_from_slice(data),
                NdpOption::PrefixInformation(pfx_info) => {
                    buffer.copy_from_slice(pfx_info.as_bytes());
                }
                NdpOption::RedirectedHeader { original_packet } => {
                    // As per RFC 4861 section 4.6.3, the first 6 bytes following the kind and length
                    // bytes are reserved so we zero them. The IP header + data field immediately
                    // follows.
                    let (reserved_bytes, original_packet_bytes) =
                        buffer.split_at_mut(REDIRECTED_HEADER_OPTION_RESERVED_BYTES_LENGTH);
                    reserved_bytes
                        .copy_from_slice(&[0; REDIRECTED_HEADER_OPTION_RESERVED_BYTES_LENGTH]);
                    original_packet_bytes.copy_from_slice(original_packet);
                }
                NdpOption::MTU(mtu) => {
                    // As per RFC 4861 section 4.6.4, the first 2 bytes following the kind and length
                    // bytes are reserved so we zero them. The MTU field immediately follows.
                    let (reserved_bytes, mtu_bytes) =
                        buffer.split_at_mut(MTU_OPTION_RESERVED_BYTES_LENGTH);
                    reserved_bytes.copy_from_slice(&[0; MTU_OPTION_RESERVED_BYTES_LENGTH]);
                    mtu_bytes.copy_from_slice(U32::new(*mtu).as_bytes());
                }
                NdpOption::RecursiveDnsServer(RecursiveDnsServer { lifetime, addresses }) => {
                    // As per RFC 8106 section 5.1, the first 2 bytes following the kind and length
                    // bytes are reserved so we zero them.
                    let (reserved_bytes, buffer) =
                        buffer.split_at_mut(RECURSIVE_DNS_SERVER_OPTION_RESERVED_BYTES_LENGTH);
                    reserved_bytes
                        .copy_from_slice(&[0; RECURSIVE_DNS_SERVER_OPTION_RESERVED_BYTES_LENGTH]);

                    // As per RFC 8106 section 5.1, the 32 bit lifetime field immediately
                    // follows the reserved field, with the list of addresses immediately
                    // following the lifetime field.
                    let (lifetime_bytes, addresses_bytes) =
                        buffer.split_at_mut(core::mem::size_of_val(lifetime));
                    lifetime_bytes.copy_from_slice(U32::new(*lifetime).as_bytes());
                    addresses_bytes.copy_from_slice(addresses.as_bytes());
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use core::convert::TryFrom;

    use byteorder::{ByteOrder, NetworkEndian};
    use net_types::ip::{Ip, IpAddress};
    use packet::serialize::Serializer;
    use packet::{InnerPacketBuilder, ParseBuffer};

    use super::*;
    use crate::icmp::{IcmpPacket, IcmpPacketBuilder, IcmpParseArgs};
    use crate::ipv6::Ipv6Packet;

    #[test]
    fn parse_serialize_redirected_header() {
        let expected_packet = [1, 2, 3, 4, 5, 6, 7, 8];
        let options = &[options::NdpOption::RedirectedHeader { original_packet: &expected_packet }];
        let serialized = OptionsSerializer::<_>::new(options.iter())
            .into_serializer()
            .serialize_vec_outer()
            .unwrap();
        // 8 bytes for the kind, length and reserved byes + the bytes for the packet.
        let mut expected = [0; 16];
        // The first two bytes are the kind and length bytes, respectively. This is then
        // followed by 6 reserved bytes.
        //
        // NDP options hold the number of bytes in units of 8 bytes.
        (&mut expected[..2]).copy_from_slice(&[4, 2]);
        (&mut expected[8..]).copy_from_slice(&expected_packet);
        assert_eq!(serialized.as_ref(), expected);

        let parsed = Options::parse(&expected[..]).unwrap();
        let parsed = parsed.iter().collect::<Vec<options::NdpOption<'_>>>();
        assert_eq!(parsed.len(), 1);
        assert_eq!(
            options::NdpOption::RedirectedHeader { original_packet: &expected_packet },
            parsed[0]
        );
    }

    #[test]
    fn parse_serialize_mtu_option() {
        let expected_mtu = 5781;
        let options = &[options::NdpOption::MTU(expected_mtu)];
        let serialized = OptionsSerializer::<_>::new(options.iter())
            .into_serializer()
            .serialize_vec_outer()
            .unwrap();
        // An MTU option is exactly 8 bytes.
        //
        // The first two bytes are the kind and length bytes, respectively. This is then
        // followed by 2 reserved bytes.
        let mut expected = [5, 1, 0, 0, 0, 0, 0, 0];
        NetworkEndian::write_u32(&mut expected[4..], expected_mtu);
        assert_eq!(serialized.as_ref(), expected);

        let parsed = Options::parse(&expected[..]).unwrap();
        let parsed = parsed.iter().collect::<Vec<options::NdpOption<'_>>>();
        assert_eq!(parsed.len(), 1);
        assert_eq!(options::NdpOption::MTU(expected_mtu), parsed[0]);
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
        // A Prefix Information option is exactly 32 bytes.
        //
        // The first two bytes are the kind and length bytes, respectively. This is then
        // immediately followed by the prefix information fields.
        let mut expected = [0; 32];
        expected[0] = 3;
        expected[1] = 4;
        (&mut expected[2..]).copy_from_slice(expected_prefix_info.as_bytes());
        assert_eq!(serialized.as_ref(), expected);

        let parsed = Options::parse(&expected[..]).unwrap();
        let parsed = parsed.iter().collect::<Vec<options::NdpOption<'_>>>();
        assert_eq!(parsed.len(), 1);
        assert_eq!(options::NdpOption::PrefixInformation(&expected_prefix_info), parsed[0]);
    }

    #[test]
    fn parse_serialize_rdnss_option() {
        let test = |addrs: &[Ipv6Addr]| {
            let lifetime = 120;
            let expected_rdnss = options::RecursiveDnsServer::new(lifetime, addrs);
            let options = &[options::NdpOption::RecursiveDnsServer(expected_rdnss.clone())];
            let serialized = OptionsSerializer::<_>::new(options.iter())
                .into_serializer()
                .serialize_vec_outer()
                .unwrap();
            // 8 bytes for the kind, length, reserved and lifetime bytes + the bytes for
            // the addresses.
            let mut expected = vec![0; 8 + addrs.len() * usize::from(Ipv6Addr::BYTES)];
            // The first two bytes are the kind and length bytes, respectively. This is then
            // followed by 2 reserved bytes.
            //
            // NDP options hold the number of bytes in units of 8 bytes.
            (&mut expected[..4]).copy_from_slice(&[
                25,
                1 + u8::try_from(addrs.len()).unwrap() * 2,
                0,
                0,
            ]);
            // The lifetime field.
            NetworkEndian::write_u32(&mut expected[4..8], lifetime);
            // The list of addressess.
            (&mut expected[8..]).copy_from_slice(addrs.as_bytes());
            assert_eq!(serialized.as_ref(), expected.as_slice());

            let parsed = Options::parse(&expected[..])
                .expect("should have parsed a valid recursive dns erver option");
            let parsed = parsed.iter().collect::<Vec<options::NdpOption<'_>>>();
            assert_eq!(parsed.len(), 1);
            assert_eq!(options::NdpOption::RecursiveDnsServer(expected_rdnss), parsed[0]);
        };
        test(&[Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16])]);
        test(&[
            Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]),
            Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17]),
        ]);
    }

    #[test]
    fn parse_serialize_rdnss_option_error() {
        let addrs = [
            Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]),
            Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17]),
        ];
        let lifetime = 120;
        // 8 bytes for the kind, length, reserved and lifetime bytes + the bytes for
        // the addresses.
        let mut buf = vec![0; 8 + addrs.len() * usize::from(Ipv6Addr::BYTES)];
        // The first two bytes are the kind and length bytes, respectively. This is then
        // followed by 2 reserved bytes.
        //
        // NDP options hold the number of bytes in units of 8 bytes.
        (&mut buf[..4]).copy_from_slice(&[25, 1 + u8::try_from(addrs.len()).unwrap() * 2, 0, 0]);
        // The lifetime field.
        NetworkEndian::write_u32(&mut buf[4..8], lifetime);
        // The list of addressess.
        (&mut buf[8..]).copy_from_slice(addrs.as_bytes());

        // Sanity check to make sure `buf` is normally valid.
        let _parsed = Options::parse(&buf[..])
            .expect("should have parsed a valid recursive dns erver option");

        // The option must hold at least 1 address.
        let _err = Options::parse(&buf[..8]).expect_err(
            "should not have parsed a recursive dns server option that has no addresses",
        );

        // The option must hold full IPv6 addresses.
        let _err = Options::parse(&buf[..buf.len()-1])
            .expect_err("should not have parsed a recursive dns server option that cuts off in the middle of an address");

        // The option must only hold unicast addresses; unspecified is not allowed.
        (&mut buf[8..8 + usize::from(Ipv6Addr::BYTES)])
            .copy_from_slice(Ipv6::UNSPECIFIED_ADDRESS.as_bytes());
        let _parsed = Options::parse(&buf[..]).expect_err(
            "should not have parsed a recursive dns erver option with an unspecified address",
        );

        // The option must only hold unicast addresses; multicast is not allowed.
        (&mut buf[8..8 + usize::from(Ipv6Addr::BYTES)])
            .copy_from_slice(Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.as_bytes());
        let _parsed = Options::parse(&buf[..]).expect_err(
            "should not have parsed a recursive dns erver option with a multicast address",
        );
    }

    #[test]
    fn parse_neighbor_solicitation() {
        use crate::icmp::testdata::ndp_neighbor::*;
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
        let collected = icmp.ndp_options().iter().collect::<Vec<options::NdpOption<'_>>>();
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
        use crate::icmp::testdata::ndp_neighbor::*;
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
        use crate::icmp::testdata::ndp_router::*;
        let mut buf = &ADVERTISEMENT_IP_PACKET_BYTES[..];
        let ip = buf.parse::<Ipv6Packet<_>>().unwrap();
        let ipv6_builder = ip.builder();
        let (src_ip, dst_ip) = (ip.src_ip(), ip.dst_ip());
        let icmp = buf
            .parse_with::<_, IcmpPacket<_, _, RouterAdvertisement>>(IcmpParseArgs::new(
                src_ip, dst_ip,
            ))
            .unwrap();
        assert_eq!(icmp.message().current_hop_limit(), HOP_LIMIT);
        assert_eq!(icmp.message().router_lifetime(), LIFETIME);
        assert_eq!(icmp.message().reachable_time(), REACHABLE_TIME);
        assert_eq!(icmp.message().retransmit_timer(), RETRANS_TIMER);

        assert_eq!(icmp.ndp_options().iter().count(), 2);

        let collected = icmp.ndp_options().iter().collect::<Vec<options::NdpOption<'_>>>();
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
