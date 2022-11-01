// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Messages used for NDP (ICMPv6).

use core::convert::{From, TryFrom};
use core::num::NonZeroU8;
use core::time::Duration;

use net_types::ip::{Ipv6, Ipv6Addr};
use zerocopy::{
    byteorder::network_endian::{U16, U32},
    AsBytes, ByteSlice, FromBytes, Unaligned,
};

use crate::icmp::{IcmpIpExt, IcmpPacket, IcmpUnusedCode};
use crate::utils::NonZeroDuration;

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

/// A non-zero lifetime conveyed through NDP.
#[derive(Copy, Clone, Debug, Eq, PartialEq, PartialOrd, Ord)]
pub enum NonZeroNdpLifetime {
    /// A finite lifetime greater than zero.
    ///
    /// Note that the finite lifetime is not statically guaranteed to be less
    /// than the infinite value representation of a field. E.g. for Prefix
    /// Information option lifetime 32-bit fields, infinity is represented as
    /// all 1s but it is possible for this variant to hold a value representing
    /// X seconds where X is >= 2^32.
    Finite(NonZeroDuration),

    /// An infinite lifetime.
    Infinite,
}

impl NonZeroNdpLifetime {
    /// Returns a `Some(NonZeroNdpLifetime)` if the passed lifetime is non-zero;
    /// otherwise `None`.
    pub fn from_u32_with_infinite(lifetime: u32) -> Option<NonZeroNdpLifetime> {
        // Per RFC 4861 section 4.6.2,
        //
        //   Valid Lifetime
        //                  32-bit unsigned integer.  The length of time in
        //                  seconds (relative to the time the packet is sent)
        //                  that the prefix is valid for the purpose of on-link
        //                  determination.  A value of all one bits
        //                  (0xffffffff) represents infinity.  The Valid
        //                  Lifetime is also used by [ADDRCONF].
        //
        //   Preferred Lifetime
        //                  32-bit unsigned integer.  The length of time in
        //                  seconds (relative to the time the packet is sent)
        //                  that addresses generated from the prefix via
        //                  stateless address autoconfiguration remain
        //                  preferred [ADDRCONF].  A value of all one bits
        //                  (0xffffffff) represents infinity.  See [ADDRCONF].
        match lifetime {
            u32::MAX => Some(NonZeroNdpLifetime::Infinite),
            finite => NonZeroDuration::new(Duration::from_secs(finite.into()))
                .map(NonZeroNdpLifetime::Finite),
        }
    }

    /// Returns the minimum finite duration.
    pub fn min_finite_duration(self, other: NonZeroDuration) -> NonZeroDuration {
        match self {
            NonZeroNdpLifetime::Finite(lifetime) => core::cmp::min(lifetime, other),
            NonZeroNdpLifetime::Infinite => other,
        }
    }
}

/// A records parser for NDP options.
///
/// See [`Options`] for more details.
///
/// [`Options`]: packet::records::options::Options
pub type Options<B> = packet::records::options::Options<B, options::NdpOptionsImpl>;

/// A builder for a sequence of NDP options.
///
/// See [`OptionSequenceBuilder`] for more details.
///
/// [`OptionSequenceBuilder`]: packet::records::options::OptionSequenceBuilder
pub type OptionSequenceBuilder<'a, I> =
    packet::records::options::OptionSequenceBuilder<options::NdpOptionBuilder<'a>, I>;

/// An NDP Router Solicitation.
#[derive(Copy, Clone, Default, Debug, FromBytes, AsBytes, Unaligned, PartialEq, Eq)]
#[repr(C)]
pub struct RouterSolicitation {
    _reserved: [u8; 4],
}

impl_icmp_message!(Ipv6, RouterSolicitation, RouterSolicitation, IcmpUnusedCode, Options<B>);

/// The preference for a route as defined by [RFC 4191 section 2.1].
///
/// [RFC 4191 section 2.1]: https://datatracker.ietf.org/doc/html/rfc4191#section-2.1
#[allow(missing_docs)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum RoutePreference {
    // We don't want to store invalid states like Reserved, as this MUST NOT be sent nor processed.
    // From RFC 4191 section 2.1:
    //   10      Reserved - MUST NOT be sent
    //   ...
    //   If the Reserved (10) value is received, the Route Information Option MUST be ignored.
    High,
    Medium,
    Low,
}

impl Default for RoutePreference {
    fn default() -> RoutePreference {
        // As per RFC 4191 section 2.1,
        //
        //   Preference values are encoded as a two-bit signed integer, as
        //   follows:
        //
        //      01      High
        //      00      Medium (default)
        //      11      Low
        //      10      Reserved - MUST NOT be sent
        RoutePreference::Medium
    }
}

impl From<RoutePreference> for u8 {
    fn from(v: RoutePreference) -> u8 {
        // As per RFC 4191 section 2.1,
        //
        //   Preference values are encoded as a two-bit signed integer, as
        //   follows:
        //
        //      01      High
        //      00      Medium (default)
        //      11      Low
        //      10      Reserved - MUST NOT be sent
        match v {
            RoutePreference::High => 0b01,
            RoutePreference::Medium => 0b00,
            RoutePreference::Low => 0b11,
        }
    }
}

impl TryFrom<u8> for RoutePreference {
    type Error = ();

    fn try_from(v: u8) -> Result<Self, Self::Error> {
        // As per RFC 4191 section 2.1,
        //
        //   Preference values are encoded as a two-bit signed integer, as
        //   follows:
        //
        //      01      High
        //      00      Medium (default)
        //      11      Low
        //      10      Reserved - MUST NOT be sent
        match v {
            0b01 => Ok(RoutePreference::High),
            0b00 => Ok(RoutePreference::Medium),
            0b11 => Ok(RoutePreference::Low),
            _ => Err(()),
        }
    }
}

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

    // As per RFC 4191 section 2.2,
    //
    //      0                   1                   2                   3
    //      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    //     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //     |     Type      |     Code      |          Checksum             |
    //     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //     | Cur Hop Limit |M|O|H|Prf|Resvd|       Router Lifetime         |
    //     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //     |                         Reachable Time                        |
    //     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //     |                          Retrans Timer                        |
    //     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //
    //  Fields:
    //
    //   Prf (Default Router Preference)
    //            2-bit signed integer.  Indicates whether to prefer this
    //            router over other default routers.  If the Router Lifetime
    //            is zero, the preference value MUST be set to (00) by the
    //            sender and MUST be ignored by the receiver.  If the Reserved
    //            (10) value is received, the receiver MUST treat the value as
    //            if it were (00).
    const DEFAULT_ROUTER_PREFERENCE_SHIFT: u8 = 3;
    const DEFAULT_ROUTER_PREFERENCE_MASK: u8 = 0b11 << Self::DEFAULT_ROUTER_PREFERENCE_SHIFT;

    /// Creates a new Router Advertisement with the specified field values.
    ///
    /// Equivalent to calling `with_prf` with a default preference value.
    pub fn new(
        current_hop_limit: u8,
        managed_flag: bool,
        other_config_flag: bool,
        router_lifetime: u16,
        reachable_time: u32,
        retransmit_timer: u32,
    ) -> Self {
        Self::with_prf(
            current_hop_limit,
            managed_flag,
            other_config_flag,
            RoutePreference::default(),
            router_lifetime,
            reachable_time,
            retransmit_timer,
        )
    }

    /// Creates a new Router Advertisement with the specified field values.
    pub fn with_prf(
        current_hop_limit: u8,
        managed_flag: bool,
        other_config_flag: bool,
        preference: RoutePreference,
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

        configuration_mo |= (u8::from(preference) << Self::DEFAULT_ROUTER_PREFERENCE_SHIFT)
            & Self::DEFAULT_ROUTER_PREFERENCE_MASK;

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

    use net_types::ip::{IpAddress as _, Ipv6Addr, Subnet, SubnetError};
    use net_types::UnicastAddress;
    use nonzero_ext::nonzero;
    use packet::records::options::{
        LengthEncoding, OptionBuilder, OptionLayout, OptionParseErr, OptionParseLayout, OptionsImpl,
    };
    use packet::BufferView as _;
    use zerocopy::byteorder::{network_endian::U32, ByteOrder, NetworkEndian};
    use zerocopy::{AsBytes, FromBytes, LayoutVerified, Unaligned};

    use super::NonZeroNdpLifetime;
    use crate::utils::NonZeroDuration;

    /// A value representing an infinite lifetime for various NDP options'
    /// lifetime fields.
    pub const INFINITE_LIFETIME: NonZeroDuration =
        NonZeroDuration::from_nonzero_secs(nonzero!(core::u32::MAX as u64));

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

    /// The number of reserved bits immediately following (on the right of) the preference.
    ///
    /// See [RFC 4191 section 2.3] for more information.
    ///
    /// [RFC 4191 section 2.3]: https://tools.ietf.org/html/rfc4191#section-2.3
    const ROUTE_INFORMATION_PREFERENCE_RESERVED_BITS_RIGHT: u8 = 3;

    /// A mask to keep only the valid bits for the preference in the Route Information option.
    ///
    /// See [RFC 4191 section 2.3] for more information.
    ///
    /// [RFC 4191 section 2.3]: https://tools.ietf.org/html/rfc4191#section-2.3
    const ROUTE_INFORMATION_PREFERENCE_MASK: u8 = 0x18;

    /// The length of an NDP option is specified in units of 8 octets.
    ///
    /// See [RFC 4861 section 4.6] for more information.
    ///
    /// [RFC 4861 section 4.6]: https://tools.ietf.org/html/rfc4861#section-4.6
    const OPTION_BYTES_PER_LENGTH_UNIT: usize = 8;

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

    /// The first 6 bytes of the Route Information option following the Type and
    /// Length fields.
    ///
    /// As per [RFC 4191 section 2.3],
    ///
    /// ```text
    ///   Route Information Option
    ///
    ///      0                   1                   2                   3
    ///       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    ///      |     Type      |    Length     | Prefix Length |Resvd|Prf|Resvd|
    ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    ///      |                        Route Lifetime                         |
    ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    ///      |                   Prefix (Variable Length)                    |
    ///      .                                                               .
    ///      .                                                               .
    ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    /// ```
    ///
    /// [RFC 4191 section 2.3]: https://datatracker.ietf.org/doc/html/rfc4191#section-2.3
    #[derive(FromBytes, AsBytes, Unaligned)]
    #[repr(C)]
    struct RouteInformationHeader {
        prefix_length: u8,
        flags: u8,
        route_lifetime: U32,
    }

    impl RouteInformationHeader {
        // As per RFC 4191 section 2.3,
        //
        //   Route Information Option
        //
        //      0                   1                   2                   3
        //       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        //      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //      |     Type      |    Length     | Prefix Length |Resvd|Prf|Resvd|
        //      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //      |                        Route Lifetime                         |
        //      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //      |                   Prefix (Variable Length)                    |
        //      .                                                               .
        //      .                                                               .
        //      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        const PREFERENCE_SHIFT: u8 = 3;
        const PREFERENCE_MASK: u8 = 0b11 << Self::PREFERENCE_SHIFT;

        fn set_preference(&mut self, preference: super::RoutePreference) {
            let preference: u8 = preference.into();

            self.flags &= !Self::PREFERENCE_MASK;
            self.flags |= (preference << Self::PREFERENCE_SHIFT) & Self::PREFERENCE_MASK;
        }
    }

    /// Builder for a Route Information option.
    ///
    /// See [RFC 4191 section 2.3].
    ///
    /// [RFC 4191 section 2.3]: https://datatracker.ietf.org/doc/html/rfc4191#section-2.3
    #[derive(Debug, PartialEq, Eq)]
    pub struct RouteInformation {
        prefix: Subnet<Ipv6Addr>,
        route_lifetime_seconds: u32,
        preference: super::RoutePreference,
    }

    impl RouteInformation {
        /// Returns a new Route Information option builder.
        pub fn new(
            prefix: Subnet<Ipv6Addr>,
            route_lifetime_seconds: u32,
            preference: super::RoutePreference,
        ) -> Self {
            Self { prefix, route_lifetime_seconds, preference }
        }

        /// The prefix represented as a [`Subnet`].
        pub fn prefix(&self) -> &Subnet<Ipv6Addr> {
            &self.prefix
        }

        /// The preference of the route.
        pub fn preference(&self) -> super::RoutePreference {
            self.preference
        }

        fn prefix_bytes_len(&self) -> usize {
            let RouteInformation { prefix, route_lifetime_seconds: _, preference: _ } = self;

            let prefix_length = prefix.prefix();
            // As per RFC 4191 section 2.3,
            //
            //    Length     8-bit unsigned integer.  The length of the option
            //               (including the Type and Length fields) in units of 8
            //               octets.  The Length field is 1, 2, or 3 depending on the
            //               Prefix Length.  If Prefix Length is greater than 64, then
            //               Length must be 3.  If Prefix Length is greater than 0,
            //               then Length must be 2 or 3.  If Prefix Length is zero,
            //               then Length must be 1, 2, or 3.
            //
            // This function only returns the length of the prefix bytes in units of
            // 1 octet.
            if prefix_length == 0 {
                0
            } else if prefix_length <= 64 {
                core::mem::size_of::<Ipv6Addr>() / 2
            } else {
                core::mem::size_of::<Ipv6Addr>()
            }
        }

        fn serialized_len(&self) -> usize {
            core::mem::size_of::<RouteInformationHeader>() + self.prefix_bytes_len()
        }

        fn serialize(&self, buffer: &mut [u8]) {
            let (mut hdr, buffer) =
                LayoutVerified::<_, RouteInformationHeader>::new_from_prefix(buffer)
                    .expect("expected buffer to hold enough bytes for serialization");

            let prefix_bytes_len = self.prefix_bytes_len();
            let RouteInformation { prefix, route_lifetime_seconds, preference } = self;

            hdr.prefix_length = prefix.prefix();
            hdr.set_preference(*preference);
            hdr.route_lifetime.set(*route_lifetime_seconds);
            buffer[..prefix_bytes_len]
                .copy_from_slice(&prefix.network().bytes()[..prefix_bytes_len])
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
        /// `None` indicates that the prefix has no valid lifetime and should
        /// not be considered valid.
        pub fn valid_lifetime(&self) -> Option<NonZeroNdpLifetime> {
            NonZeroNdpLifetime::from_u32_with_infinite(self.valid_lifetime.get())
        }

        /// Get the length of time (relative to the time the packet is sent) that
        /// addresses generated from the prefix via SLAAC remains preferred.
        ///
        /// `None` indicates that the prefix has no preferred lifetime and
        /// should not be considered preferred.
        pub fn preferred_lifetime(&self) -> Option<NonZeroNdpLifetime> {
            NonZeroNdpLifetime::from_u32_with_infinite(self.preferred_lifetime.get())
        }

        /// An IPv6 address or a prefix of an IPv6 address.
        ///
        /// The number of valid leading bits in this prefix is available
        /// from [`PrefixInformation::prefix_length`];
        // TODO(https://fxbug.dev/91764): Consider merging prefix and prefix_length and return a
        // Subnet.
        pub fn prefix(&self) -> &Ipv6Addr {
            &self.prefix
        }

        /// Gets the prefix as a [`Subnet`].
        pub fn subnet(&self) -> Result<Subnet<Ipv6Addr>, SubnetError> {
            Subnet::new(self.prefix, self.prefix_length)
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
            RouteInformation, 24, "Route Information";
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

        Mtu(u32),

        RecursiveDnsServer(RecursiveDnsServer<'a>),
        RouteInformation(RouteInformation),
    }

    /// An implementation of [`OptionsImpl`] for NDP options.
    #[derive(Debug)]
    pub struct NdpOptionsImpl;

    impl<'a> OptionLayout for NdpOptionsImpl {
        type KindLenField = u8;

        // For NDP options the length should be multiplied by 8.
        const LENGTH_ENCODING: LengthEncoding =
            LengthEncoding::TypeLengthValue { option_len_multiplier: nonzero!(8usize) };
    }

    impl OptionParseLayout for NdpOptionsImpl {
        // TODO(fxbug.dev/52288): Return more verbose logs on parsing errors.
        type Error = OptionParseErr;

        // NDP options don't have END_OF_OPTIONS or NOP.
        const END_OF_OPTIONS: Option<u8> = None;
        const NOP: Option<u8> = None;
    }

    impl<'a> OptionsImpl<'a> for NdpOptionsImpl {
        type Option = NdpOption<'a>;

        fn parse(kind: u8, mut data: &'a [u8]) -> Result<Option<NdpOption<'a>>, OptionParseErr> {
            let kind = if let Ok(k) = NdpOptionType::try_from(kind) {
                k
            } else {
                return Ok(None);
            };

            let opt = match kind {
                NdpOptionType::SourceLinkLayerAddress => NdpOption::SourceLinkLayerAddress(data),
                NdpOptionType::TargetLinkLayerAddress => NdpOption::TargetLinkLayerAddress(data),
                NdpOptionType::PrefixInformation => {
                    let data =
                        LayoutVerified::<_, PrefixInformation>::new(data).ok_or(OptionParseErr)?;
                    NdpOption::PrefixInformation(data.into_ref())
                }
                NdpOptionType::RedirectedHeader => NdpOption::RedirectedHeader {
                    original_packet: &data[REDIRECTED_HEADER_OPTION_RESERVED_BYTES_LENGTH..],
                },
                NdpOptionType::Mtu => NdpOption::Mtu(NetworkEndian::read_u32(
                    &data[MTU_OPTION_RESERVED_BYTES_LENGTH..],
                )),
                NdpOptionType::RecursiveDnsServer => {
                    if data.len() < MIN_RECURSIVE_DNS_SERVER_OPTION_LENGTH {
                        return Err(OptionParseErr);
                    }

                    // Skip the reserved bytes which immediately follow the kind and length
                    // bytes.
                    let (_, data) =
                        data.split_at(RECURSIVE_DNS_SERVER_OPTION_RESERVED_BYTES_LENGTH);

                    // As per RFC 8106 section 5.1, the 32 bit lifetime field immediately
                    // follows the reserved field.
                    let (lifetime, data) =
                        LayoutVerified::<_, U32>::new_from_prefix(data).ok_or(OptionParseErr)?;

                    // As per RFC 8106 section 5.1, the list of addresses immediately
                    // follows the lifetime field.
                    let addresses = LayoutVerified::<_, [Ipv6Addr]>::new_slice_unaligned(data)
                        .ok_or(OptionParseErr)?
                        .into_slice();

                    // As per RFC 8106 section 5.3.1, the addresses should all be unicast.
                    if !addresses.iter().all(UnicastAddress::is_unicast) {
                        return Err(OptionParseErr);
                    }

                    NdpOption::RecursiveDnsServer(RecursiveDnsServer::new(
                        lifetime.get(),
                        addresses,
                    ))
                }
                NdpOptionType::RouteInformation => {
                    // RouteInfoFixed represents the part of the RouteInformation option
                    // with a known and fixed length. See RFC 4191 section 2.3.
                    #[derive(FromBytes, Unaligned)]
                    #[repr(C)]
                    struct RouteInfoFixed {
                        prefix_length: u8,
                        preference_raw: u8,
                        route_lifetime_seconds: U32,
                    }

                    let mut buf = &mut data;

                    let fixed = buf.take_obj_front::<RouteInfoFixed>().ok_or(OptionParseErr)?;

                    // The preference is preceded and followed by two 3-bit reserved fields.
                    let preference = super::RoutePreference::try_from(
                        (fixed.preference_raw & ROUTE_INFORMATION_PREFERENCE_MASK)
                            >> ROUTE_INFORMATION_PREFERENCE_RESERVED_BITS_RIGHT,
                    )
                    .map_err(|()| OptionParseErr)?;

                    // We need to check whether the remaining buffer length storing the prefix is
                    // valid.
                    // From RFC 4191 section 2.3:
                    //   The length of the option (including the Type and Length fields) in units
                    //   of 8 octets.  The Length field is 1, 2, or 3 depending on the Prefix
                    //   Length.  If Prefix Length is greater than 64, then Length must be 3.  If
                    //   Prefix Length is greater than 0, then Length must be 2 or 3.  If Prefix
                    //   Length is zero, then Length must be 1, 2, or 3.
                    // The RFC refers to the length of the body which is Route Lifetime + Prefix,
                    // i.e. the prefix contained in the buffer can have a length from 0 to 2
                    // (included) octets i.e. 0 to 16 bytes.
                    let buf_len = buf.len();
                    if buf_len % OPTION_BYTES_PER_LENGTH_UNIT != 0 {
                        return Err(OptionParseErr);
                    }
                    let length = buf_len / OPTION_BYTES_PER_LENGTH_UNIT;
                    match (fixed.prefix_length, length) {
                        (65..=128, 2) => {}
                        (1..=64, 1 | 2) => {}
                        (0, 0 | 1 | 2) => {}
                        _ => return Err(OptionParseErr),
                    }

                    let mut prefix_buf = [0; 16];
                    // It is safe to copy because we validated the remaining length of the buffer.
                    prefix_buf[..buf_len].copy_from_slice(&buf);
                    let prefix = Ipv6Addr::from_bytes(prefix_buf);

                    NdpOption::RouteInformation(RouteInformation::new(
                        Subnet::new(prefix, fixed.prefix_length).map_err(|_| OptionParseErr)?,
                        fixed.route_lifetime_seconds.get(),
                        preference,
                    ))
                }
            };

            Ok(Some(opt))
        }
    }

    /// Builder for NDP options that may be found in NDP messages.
    #[allow(missing_docs)]
    #[derive(Debug)]
    pub enum NdpOptionBuilder<'a> {
        SourceLinkLayerAddress(&'a [u8]),
        TargetLinkLayerAddress(&'a [u8]),
        PrefixInformation(PrefixInformation),

        RedirectedHeader { original_packet: &'a [u8] },

        Mtu(u32),

        RouteInformation(RouteInformation),
        RecursiveDnsServer(RecursiveDnsServer<'a>),
    }

    impl<'a> From<&NdpOptionBuilder<'a>> for NdpOptionType {
        fn from(v: &NdpOptionBuilder<'a>) -> Self {
            match v {
                NdpOptionBuilder::SourceLinkLayerAddress(_) => {
                    NdpOptionType::SourceLinkLayerAddress
                }
                NdpOptionBuilder::TargetLinkLayerAddress(_) => {
                    NdpOptionType::TargetLinkLayerAddress
                }
                NdpOptionBuilder::PrefixInformation(_) => NdpOptionType::PrefixInformation,
                NdpOptionBuilder::RedirectedHeader { .. } => NdpOptionType::RedirectedHeader,
                NdpOptionBuilder::Mtu { .. } => NdpOptionType::Mtu,
                NdpOptionBuilder::RouteInformation(_) => NdpOptionType::RouteInformation,
                NdpOptionBuilder::RecursiveDnsServer(_) => NdpOptionType::RecursiveDnsServer,
            }
        }
    }

    impl<'a> OptionBuilder for NdpOptionBuilder<'a> {
        type Layout = NdpOptionsImpl;

        fn serialized_len(&self) -> usize {
            match self {
                NdpOptionBuilder::SourceLinkLayerAddress(data)
                | NdpOptionBuilder::TargetLinkLayerAddress(data) => data.len(),
                NdpOptionBuilder::PrefixInformation(_) => PREFIX_INFORMATION_OPTION_LENGTH,
                NdpOptionBuilder::RedirectedHeader { original_packet } => {
                    REDIRECTED_HEADER_OPTION_RESERVED_BYTES_LENGTH + original_packet.len()
                }
                NdpOptionBuilder::Mtu(_) => MTU_OPTION_LENGTH,
                NdpOptionBuilder::RouteInformation(o) => o.serialized_len(),
                NdpOptionBuilder::RecursiveDnsServer(RecursiveDnsServer {
                    lifetime,
                    addresses,
                }) => {
                    RECURSIVE_DNS_SERVER_OPTION_RESERVED_BYTES_LENGTH
                        + core::mem::size_of_val(lifetime)
                        + core::mem::size_of_val(*addresses)
                }
            }
        }

        fn option_kind(&self) -> u8 {
            NdpOptionType::from(self).into()
        }

        fn serialize_into(&self, buffer: &mut [u8]) {
            match self {
                NdpOptionBuilder::SourceLinkLayerAddress(data)
                | NdpOptionBuilder::TargetLinkLayerAddress(data) => buffer.copy_from_slice(data),
                NdpOptionBuilder::PrefixInformation(pfx_info) => {
                    buffer.copy_from_slice(pfx_info.as_bytes());
                }
                NdpOptionBuilder::RedirectedHeader { original_packet } => {
                    // As per RFC 4861 section 4.6.3, the first 6 bytes following the kind and length
                    // bytes are reserved so we zero them. The IP header + data field immediately
                    // follows.
                    let (reserved_bytes, original_packet_bytes) =
                        buffer.split_at_mut(REDIRECTED_HEADER_OPTION_RESERVED_BYTES_LENGTH);
                    reserved_bytes
                        .copy_from_slice(&[0; REDIRECTED_HEADER_OPTION_RESERVED_BYTES_LENGTH]);
                    original_packet_bytes.copy_from_slice(original_packet);
                }
                NdpOptionBuilder::Mtu(mtu) => {
                    // As per RFC 4861 section 4.6.4, the first 2 bytes following the kind and length
                    // bytes are reserved so we zero them. The MTU field immediately follows.
                    let (reserved_bytes, mtu_bytes) =
                        buffer.split_at_mut(MTU_OPTION_RESERVED_BYTES_LENGTH);
                    reserved_bytes.copy_from_slice(&[0; MTU_OPTION_RESERVED_BYTES_LENGTH]);
                    mtu_bytes.copy_from_slice(U32::new(*mtu).as_bytes());
                }
                NdpOptionBuilder::RouteInformation(p) => p.serialize(buffer),
                NdpOptionBuilder::RecursiveDnsServer(RecursiveDnsServer {
                    lifetime,
                    addresses,
                }) => {
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

    use net_types::ip::{Ip, IpAddress, Subnet};
    use packet::serialize::Serializer;
    use packet::{InnerPacketBuilder, ParseBuffer};
    use test_case::test_case;
    use zerocopy::byteorder::{ByteOrder, NetworkEndian};
    use zerocopy::LayoutVerified;

    use super::*;
    use crate::icmp::{IcmpPacket, IcmpPacketBuilder, IcmpParseArgs};
    use crate::ipv6::{Ipv6Header, Ipv6Packet};

    #[test]
    fn parse_serialize_redirected_header() {
        let expected_packet = [1, 2, 3, 4, 5, 6, 7, 8];
        let options =
            &[options::NdpOptionBuilder::RedirectedHeader { original_packet: &expected_packet }];
        let serialized = OptionSequenceBuilder::new(options.iter())
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
        let options = &[options::NdpOptionBuilder::Mtu(expected_mtu)];
        let serialized = OptionSequenceBuilder::new(options.iter())
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
        assert_eq!(options::NdpOption::Mtu(expected_mtu), parsed[0]);
    }

    #[test]
    fn parse_serialize_prefix_option() {
        let expected_prefix_info = options::PrefixInformation::new(
            120,
            true,
            false,
            100,
            100,
            Ipv6Addr::from([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 0]),
        );
        let options = &[options::NdpOptionBuilder::PrefixInformation(expected_prefix_info.clone())];
        let serialized = OptionSequenceBuilder::new(options.iter())
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
            let options = &[options::NdpOptionBuilder::RecursiveDnsServer(expected_rdnss.clone())];
            let serialized = OptionSequenceBuilder::new(options.iter())
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
        test(&[Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16])]);
        test(&[
            Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]),
            Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17]),
        ]);
    }

    #[test]
    fn parse_serialize_rdnss_option_error() {
        let addrs = [
            Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]),
            Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17]),
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
        let mut buf = SOLICITATION_IP_PACKET_BYTES;
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
        let option_builders =
            [options::NdpOptionBuilder::SourceLinkLayerAddress(&SOURCE_LINK_LAYER_ADDRESS)];
        let serialized = OptionSequenceBuilder::new(option_builders.iter())
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
        let mut buf = ADVERTISEMENT_IP_PACKET_BYTES;
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
        use crate::icmp::ndp::options::RouteInformation;
        use crate::icmp::testdata::ndp_router::*;

        let mut buf = ADVERTISEMENT_IP_PACKET_BYTES;
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

        assert_eq!(icmp.ndp_options().iter().count(), 5);

        let collected = icmp.ndp_options().iter().collect::<Vec<options::NdpOption<'_>>>();
        for option in collected.iter() {
            match option {
                options::NdpOption::SourceLinkLayerAddress(address) => {
                    assert_eq!(address, &SOURCE_LINK_LAYER_ADDRESS);
                }
                options::NdpOption::PrefixInformation(info) => {
                    assert_eq!(info.on_link_flag(), PREFIX_INFO_ON_LINK_FLAG);
                    assert_eq!(
                        info.autonomous_address_configuration_flag(),
                        PREFIX_INFO_AUTONOMOUS_ADDRESS_CONFIGURATION_FLAG
                    );
                    assert_eq!(
                        info.valid_lifetime(),
                        NonZeroNdpLifetime::from_u32_with_infinite(
                            PREFIX_INFO_VALID_LIFETIME_SECONDS
                        )
                    );
                    assert_eq!(
                        info.preferred_lifetime(),
                        NonZeroNdpLifetime::from_u32_with_infinite(
                            PREFIX_INFO_PREFERRED_LIFETIME_SECONDS
                        )
                    );
                    assert_eq!(info.prefix_length(), PREFIX_INFO_PREFIX.prefix());
                    assert_eq!(info.prefix(), &PREFIX_INFO_PREFIX.network());
                }
                options::NdpOption::RouteInformation(_) => {
                    // Tested below
                }
                o => panic!("Found unexpected option: {:?}", o),
            }
        }

        let mut route_information_options = collected
            .iter()
            .filter_map(|o| match o {
                options::NdpOption::RouteInformation(info) => Some(info),
                _ => None,
            })
            .collect::<Vec<&RouteInformation>>();
        // We must not make any assumptions on the order of received data, therefore we sort them.
        // From RFC 4861 section 4.6.2:
        //   Options in Neighbor Discovery packets can appear in any order; receivers MUST be
        //   prepared to process them independently of their order.
        route_information_options.sort_by_key(|o| o.prefix().prefix());
        assert_eq!(
            route_information_options,
            [
                &options::RouteInformation::new(
                    ROUTE_INFO_LOW_PREF_PREFIX,
                    ROUTE_INFO_LOW_PREF_VALID_LIFETIME_SECONDS,
                    ROUTE_INFO_LOW_PREF,
                ),
                &options::RouteInformation::new(
                    ROUTE_INFO_MEDIUM_PREF_PREFIX,
                    ROUTE_INFO_MEDIUM_PREF_VALID_LIFETIME_SECONDS,
                    ROUTE_INFO_MEDIUM_PREF,
                ),
                &options::RouteInformation::new(
                    ROUTE_INFO_HIGH_PREF_PREFIX,
                    ROUTE_INFO_HIGH_PREF_VALID_LIFETIME_SECONDS,
                    ROUTE_INFO_HIGH_PREF,
                )
            ]
        );

        let option_builders = [
            options::NdpOptionBuilder::SourceLinkLayerAddress(&SOURCE_LINK_LAYER_ADDRESS),
            options::NdpOptionBuilder::PrefixInformation(options::PrefixInformation::new(
                PREFIX_INFO_PREFIX.prefix(),
                PREFIX_INFO_ON_LINK_FLAG,
                PREFIX_INFO_AUTONOMOUS_ADDRESS_CONFIGURATION_FLAG,
                PREFIX_INFO_VALID_LIFETIME_SECONDS,
                PREFIX_INFO_PREFERRED_LIFETIME_SECONDS,
                PREFIX_INFO_PREFIX.network(),
            )),
            options::NdpOptionBuilder::RouteInformation(options::RouteInformation::new(
                ROUTE_INFO_HIGH_PREF_PREFIX,
                ROUTE_INFO_HIGH_PREF_VALID_LIFETIME_SECONDS,
                ROUTE_INFO_HIGH_PREF,
            )),
            options::NdpOptionBuilder::RouteInformation(options::RouteInformation::new(
                ROUTE_INFO_MEDIUM_PREF_PREFIX,
                ROUTE_INFO_MEDIUM_PREF_VALID_LIFETIME_SECONDS,
                ROUTE_INFO_MEDIUM_PREF,
            )),
            options::NdpOptionBuilder::RouteInformation(options::RouteInformation::new(
                ROUTE_INFO_LOW_PREF_PREFIX,
                ROUTE_INFO_LOW_PREF_VALID_LIFETIME_SECONDS,
                ROUTE_INFO_LOW_PREF,
            )),
        ];
        let serialized = OptionSequenceBuilder::new(option_builders.iter())
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

    struct SerializeRATest {
        hop_limit: u8,
        managed_flag: bool,
        other_config_flag: bool,
        preference: RoutePreference,
        router_lifetime_seconds: u16,
        reachable_time_seconds: u32,
        retransmit_timer_seconds: u32,
    }

    #[test_case(
        SerializeRATest{
            hop_limit: 1,
            managed_flag: true,
            other_config_flag: false,
            preference: RoutePreference::High,
            router_lifetime_seconds: 1_000,
            reachable_time_seconds: 1_000_000,
            retransmit_timer_seconds: 5,
        }; "test_1")]
    #[test_case(
        SerializeRATest{
            hop_limit: 64,
            managed_flag: false,
            other_config_flag: true,
            preference: RoutePreference::Low,
            router_lifetime_seconds: 5,
            reachable_time_seconds: 23425621,
            retransmit_timer_seconds: 13252521,
        }; "test_2")]
    fn serialize_router_advertisement(test: SerializeRATest) {
        let SerializeRATest {
            hop_limit,
            managed_flag,
            other_config_flag,
            preference,
            router_lifetime_seconds,
            reachable_time_seconds,
            retransmit_timer_seconds,
        } = test;

        const SRC_IP: Ipv6Addr =
            Ipv6Addr::from_bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
        const DST_IP: Ipv6Addr =
            Ipv6Addr::from_bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17]);
        let serialized = packet::EmptyBuf
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                SRC_IP,
                DST_IP,
                IcmpUnusedCode,
                RouterAdvertisement::with_prf(
                    hop_limit,
                    managed_flag,
                    other_config_flag,
                    preference,
                    router_lifetime_seconds,
                    reachable_time_seconds,
                    retransmit_timer_seconds,
                ),
            ))
            .serialize_vec_outer()
            .unwrap();

        // As per RFC 4191 section 2.2,
        //
        //      0                   1                   2                   3
        //      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        //     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //     |     Type      |     Code      |          Checksum             |
        //     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //     | Cur Hop Limit |M|O|H|Prf|Resvd|       Router Lifetime         |
        //     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //     |                         Reachable Time                        |
        //     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //     |                          Retrans Timer                        |
        //     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //
        // As  per RFC 4861 section 4.2,
        //
        //    ICMP Fields:
        //
        //      Type           134
        //
        //      Code           0
        const RA_LEN: u32 = 16;
        let mut expected = [0; RA_LEN as usize];
        expected[0] = 134;
        expected[4] = hop_limit;
        if managed_flag {
            expected[5] |= 1 << 7;
        }
        if other_config_flag {
            expected[5] |= 1 << 6;
        }
        expected[5] |= u8::from(preference) << 3;
        let (mut router_lifetime, _rest) =
            LayoutVerified::<_, U16>::new_from_prefix(&mut expected[6..]).unwrap();
        router_lifetime.set(router_lifetime_seconds);
        let (mut reachable_time, _rest) =
            LayoutVerified::<_, U32>::new_from_prefix(&mut expected[8..]).unwrap();
        reachable_time.set(reachable_time_seconds);
        let (mut retransmit_timer, _rest) =
            LayoutVerified::<_, U32>::new_from_prefix(&mut expected[12..]).unwrap();
        retransmit_timer.set(retransmit_timer_seconds);

        let mut c = internet_checksum::Checksum::new();
        // Checksum pseudo-header.
        c.add_bytes(SRC_IP.bytes());
        c.add_bytes(DST_IP.bytes());
        c.add_bytes(U32::new(RA_LEN).as_bytes());
        c.add_bytes(&[0, crate::ip::Ipv6Proto::Icmpv6.into()]);
        // Checksum actual message.
        c.add_bytes(&expected[..]);
        expected[2..4].copy_from_slice(&c.checksum()[..]);

        assert_eq!(serialized.as_ref(), &expected[..]);
    }

    struct SerializeRioTest {
        prefix_length: u8,
        route_lifetime_seconds: u32,
        preference: RoutePreference,
        expected_option_length: u8,
    }

    // As per RFC 4191 section 2.3,
    //
    //    Length     8-bit unsigned integer.  The length of the option
    //               (including the Type and Length fields) in units of 8
    //               octets.  The Length field is 1, 2, or 3 depending on the
    //               Prefix Length.  If Prefix Length is greater than 64, then
    //               Length must be 3.  If Prefix Length is greater than 0,
    //               then Length must be 2 or 3.  If Prefix Length is zero,
    //               then Length must be 1, 2, or 3.
    #[test_case(
        SerializeRioTest{
            prefix_length: 0,
            route_lifetime_seconds: 1,
            preference: RoutePreference::High,
            expected_option_length: 8,
        }; "prefix_length_0")]
    #[test_case(
        SerializeRioTest{
            prefix_length: 1,
            route_lifetime_seconds: 1000,
            preference: RoutePreference::Medium,
            expected_option_length: 16,
        }; "prefix_length_1")]
    #[test_case(
        SerializeRioTest{
            prefix_length: 64,
            route_lifetime_seconds: 100000,
            preference: RoutePreference::Low,
            expected_option_length: 16,
        }; "prefix_length_64")]
    #[test_case(
        SerializeRioTest{
            prefix_length: 65,
            route_lifetime_seconds: 1000000,
            preference: RoutePreference::Medium,
            expected_option_length: 24,
        }; "prefix_length_65")]
    #[test_case(
        SerializeRioTest{
            prefix_length: 128,
            route_lifetime_seconds: 10000000,
            preference: RoutePreference::Medium,
            expected_option_length: 24,
        }; "prefix_length_128")]
    fn serialize_route_information_option(test: SerializeRioTest) {
        const IPV6ADDR: Ipv6Addr =
            Ipv6Addr::new([0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff]);

        let SerializeRioTest {
            prefix_length,
            route_lifetime_seconds,
            preference,
            expected_option_length,
        } = test;
        let prefix = IPV6ADDR.mask(prefix_length);

        let option_builders =
            [options::NdpOptionBuilder::RouteInformation(options::RouteInformation::new(
                Subnet::new(prefix, prefix_length).unwrap(),
                route_lifetime_seconds,
                preference,
            ))];

        let serialized = OptionSequenceBuilder::new(option_builders.iter())
            .into_serializer()
            .serialize_vec_outer()
            .unwrap();

        // As per RFC 4191 section 2.3,
        //
        //   Route Information Option
        //
        //      0                   1                   2                   3
        //       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        //      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //      |     Type      |    Length     | Prefix Length |Resvd|Prf|Resvd|
        //      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //      |                        Route Lifetime                         |
        //      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //      |                   Prefix (Variable Length)                    |
        //      .                                                               .
        //      .                                                               .
        //      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //
        //   Fields:
        //
        //   Type        24
        //
        //   Length      8-bit unsigned integer.  The length of the option
        //               (including the Type and Length fields) in units of 8
        //               octets.  The Length field is 1, 2, or 3 depending on the
        //               Prefix Length.  If Prefix Length is greater than 64, then
        //               Length must be 3.  If Prefix Length is greater than 0,
        //               then Length must be 2 or 3.  If Prefix Length is zero,
        //               then Length must be 1, 2, or 3.
        let mut expected = [0; 24];
        expected[0] = 24;
        expected[1] = expected_option_length / 8;
        expected[2] = prefix_length;
        expected[3] = u8::from(preference) << 3;
        let (mut lifetime_seconds, _rest) =
            LayoutVerified::<_, U32>::new_from_prefix(&mut expected[4..]).unwrap();
        lifetime_seconds.set(route_lifetime_seconds);
        expected[8..].copy_from_slice(prefix.bytes());

        assert_eq!(serialized.as_ref(), &expected[..expected_option_length.into()]);
    }

    #[test_case(0, None)]
    #[test_case(
        1,
        Some(NonZeroNdpLifetime::Finite(NonZeroDuration::new(
            Duration::from_secs(1),
        ).unwrap()))
    )]
    #[test_case(
        u32::MAX - 1,
        Some(NonZeroNdpLifetime::Finite(NonZeroDuration::new(
            Duration::from_secs(u64::from(u32::MAX) - 1),
        ).unwrap()))
    )]
    #[test_case(u32::MAX, Some(NonZeroNdpLifetime::Infinite))]
    fn non_zero_ndp_lifetime_non_zero_or_max_u32_from_u32_with_infinite(
        t: u32,
        expected: Option<NonZeroNdpLifetime>,
    ) {
        assert_eq!(NonZeroNdpLifetime::from_u32_with_infinite(t), expected)
    }

    const MIN_NON_ZERO_DURATION: Duration = Duration::new(0, 1);
    #[test_case(
        NonZeroNdpLifetime::Infinite,
        NonZeroDuration::new(MIN_NON_ZERO_DURATION).unwrap(),
        NonZeroDuration::new(MIN_NON_ZERO_DURATION).unwrap()
    )]
    #[test_case(
        NonZeroNdpLifetime::Infinite,
        NonZeroDuration::new(Duration::MAX).unwrap(),
        NonZeroDuration::new(Duration::MAX).unwrap()
    )]
    #[test_case(
        NonZeroNdpLifetime::Finite(NonZeroDuration::new(
            Duration::from_secs(2)).unwrap()
        ),
        NonZeroDuration::new(Duration::from_secs(1)).unwrap(),
        NonZeroDuration::new(Duration::from_secs(1)).unwrap()
    )]
    #[test_case(
        NonZeroNdpLifetime::Finite(NonZeroDuration::new(
            Duration::from_secs(3)).unwrap()
        ),
        NonZeroDuration::new(Duration::from_secs(4)).unwrap(),
        NonZeroDuration::new(Duration::from_secs(3)).unwrap()
    )]
    fn non_zero_ndp_lifetime_min_finite_duration(
        lifetime: NonZeroNdpLifetime,
        duration: NonZeroDuration,
        expected: NonZeroDuration,
    ) {
        assert_eq!(lifetime.min_finite_duration(duration), expected)
    }
}
