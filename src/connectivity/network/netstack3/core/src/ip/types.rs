// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Public and private types for the IP layer
///
/// This module contains types that represent IP addresses, subnets, and IP
/// address + subnet pairs. The reason that we have so many types is to provide
/// different safety guarantees, while allowing abstraction over different IP
/// version. Specifically, we want to be able to represent both concrete IP
/// versions (an IPv4 address, for instance), as well as enums that could be
/// either a v4 or v6 type. However, just providing concrete types and enums
/// doesn't give us the safety guarantees that we want. Specifically, if we have
/// a function that takes, for instance, and IP address and a subnet:
///
/// ```rust,ignore
/// fn foo(addr: IpAddr, subnet: SubnetEither) { ... }
/// ```
///
/// The problem with this is that we cannot guarantee at the type level that
/// both the IP address and the subnet are the same IP version - we would need
/// to raise a runtime error if they did not match. This is why we additionally
/// have types that allow specifying the IP version as a type parameter - we can
/// rewrite the above function as:
///
/// ```rust,ignore
/// fn foo<A: IpAddress>(addr: A, subnet: Subnet<A>) { ... }
/// ```
///
/// This way, calling `foo()` with different IP versions for the different
/// paramaters is impossible, since such a program would not typecheck.
///
/// We provide the following types:
///
/// # IP Addresses
///
/// * `Ipv4Addr`: A concrete IPv4 address
/// * `Ipv6Addr`: A concrete IPv6 address
/// * `IpAddr`: An enum representing either a v4 or v6 address.
/// * `IpAddress`: An IP address trait that can be used to bound a type
///   parameter.
///
/// # Subnets
///
/// * `Subnet`: A v4 or v6 subnet, as specificed by the type parameter.
/// * `SubnetEither`: An enum of either a v4 subnet or a v6 subnet.
///
/// # Forwarding Entries
///
/// * `EntryDest<A>`: A forwarding entry destination, A is v4 or v6.
/// * `EntryEither`: A subnet and `EntryDest` paired, both either v4 or v6.
///
/// # Address + Subnet Pairs:
///
/// * `AddrSubnet`: A v4 or v6 subnet + address pair, as specificed by the type
///   parameter.
/// * `AddrSubnetEither`: An enum of either a v4 or a v6 subnet + address pair.
///
/// # Public vs Private
///
/// One subtlety worth calling out: the `internal::IpAddress` trait is private,
/// and defines a lot of functionality which is not relevant to those outside
/// the core. The `internal::ext` submodule contains its own `IpAddress` trait
/// which is a subtrait of the private one, and is the trait which is exported
/// publicly from the root module. There is a blanket impl so that the compiler
/// knows that all `IpAddress` types are also `ext::IpAddress`, and vice versa.
// Unlike most items, which can be simply marked as `pub` if they are going to
// be re-exported from the root, and `pub(crate)` otherwise, the situation here
// is a bit more complicated. The `internal::ext::IpAddress` trait (which is
// re-exported from the root) is a sub-trait of the `internal::IpAddress` trait
// (which is not). If we were to mark `internal::IpAddress` as `pub(crate)`, we
// would get a private-in-public error. Once we mark it `pub`, the same goes for
// every type and trait used in it transitively. All of these would be
// exceptions to the normal "pub only if it's re-exported from the root" rule,
// and it would be verbose and brittle to document each of them as such.
//
// Instead, we take a different approach. Everything in the `internal` module
// which needs to be marked `pub` is. However, the source of truth for what's
// exposed publicly are the following two lines: `pub(crate) use` for everything
// in `internal`, and `pub use` for a selected set of items which we actually
// want to be re-exported. This gives us one place to keep track of what we
// intend to re-export, and also allows us to enforce that programmatically (the
// `pub(crate) use` will prevent items from being accidentally re-exported).
pub(crate) use self::internal::*;
pub use self::internal::{
    AddrSubnet, AddrSubnetEither, EntryDest, EntryEither, Ipv4Addr, Ipv6Addr, Subnet, SubnetEither,
};

mod internal {
    use std::fmt::{self, Debug, Display, Formatter};
    use std::hash::Hash;
    use std::net;

    use byteorder::{ByteOrder, NetworkEndian};
    use never::Never;
    use packet::{PacketBuilder, ParsablePacket};
    use zerocopy::{AsBytes, ByteSlice, ByteSliceMut, FromBytes, Unaligned};

    use crate::device::DeviceId;
    use crate::error::ParseError;
    use crate::wire::ipv4::{Ipv4Packet, Ipv4PacketBuilder};
    use crate::wire::ipv6::{Ipv6Packet, Ipv6PacketBuilder};

    /// An IP protocol version.
    #[allow(missing_docs)]
    #[derive(Copy, Clone, Eq, PartialEq, Debug)]
    pub enum IpVersion {
        V4,
        V6,
    }

    /// An IP address.
    #[allow(missing_docs)]
    #[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
    pub enum IpAddr {
        V4(Ipv4Addr),
        V6(Ipv6Addr),
    }

    impl<A: ext::IpAddress> From<A> for IpAddr {
        fn from(addr: A) -> IpAddr {
            addr.into_ip_addr()
        }
    }

    impl From<net::IpAddr> for IpAddr {
        fn from(addr: net::IpAddr) -> IpAddr {
            match addr {
                net::IpAddr::V4(addr) => IpAddr::V4(addr.into()),
                net::IpAddr::V6(addr) => IpAddr::V6(addr.into()),
            }
        }
    }

    impl IpVersion {
        /// The number for this IP protocol version.
        ///
        /// 4 for `V4` and 6 for `V6`.
        pub(crate) fn version_number(self) -> u8 {
            match self {
                IpVersion::V4 => 4,
                IpVersion::V6 => 6,
            }
        }

        /// Is this IPv4?
        pub(crate) fn is_v4(self) -> bool {
            self == IpVersion::V4
        }

        /// Is this IPv6?
        pub(crate) fn is_v6(self) -> bool {
            self == IpVersion::V6
        }
    }

    mod sealed {
        // Ensure that only Ipv4 and Ipv6 can implement IpVersion and that only
        // Ipv4Addr and Ipv6Addr can implement IpAddress.
        pub trait Sealed {}

        impl Sealed for super::Ipv4 {}
        impl Sealed for super::Ipv6 {}
        impl Sealed for super::Ipv4Addr {}
        impl Sealed for super::Ipv6Addr {}
    }

    /// A trait for IP protocol versions.
    ///
    /// `Ip` encapsulates the details of a version of the IP protocol. It
    /// includes the `IpVersion` enum (`VERSION`) and address type (`Addr`). It
    /// is implemented by `Ipv4` and `Ipv6`.
    pub trait Ip: Sized + self::sealed::Sealed {
        /// The IP version.
        ///
        /// `V4` for IPv4 and `V6` for IPv6.
        const VERSION: IpVersion;

        /// The unspecified address.
        ///
        /// This is 0.0.0.0 for IPv4 and :: for IPv6.
        const UNSPECIFIED_ADDRESS: Self::Addr;

        /// The default loopback address.
        ///
        /// When sending packets to a loopback interface, this address is used
        /// as the source address. It is an address in the loopback subnet.
        const LOOPBACK_ADDRESS: Self::Addr;

        /// The subnet of loopback addresses.
        ///
        /// Addresses in this subnet must not appear outside a host, and may
        /// only be used for loopback interfaces.
        const LOOPBACK_SUBNET: Subnet<Self::Addr>;

        /// The subnet of multicast addresses.
        const MULTICAST_SUBNET: Subnet<Self::Addr>;

        /// The address type for this IP version.
        ///
        /// `Ipv4Addr` for IPv4 and `Ipv6Addr` for IPv6.
        type Addr: IpAddress<Version = Self>;
    }

    /// IPv4.
    ///
    /// `Ipv4` implements `Ip` for IPv4.
    #[derive(Debug, Default)]
    pub struct Ipv4;

    impl Ip for Ipv4 {
        const VERSION: IpVersion = IpVersion::V4;
        const UNSPECIFIED_ADDRESS: Ipv4Addr = Ipv4Addr::new([0, 0, 0, 0]);
        // https://tools.ietf.org/html/rfc5735#section-3
        const LOOPBACK_ADDRESS: Ipv4Addr = Ipv4Addr::new([127, 0, 0, 1]);
        const LOOPBACK_SUBNET: Subnet<Ipv4Addr> =
            Subnet { network: Ipv4Addr::new([127, 0, 0, 0]), prefix: 8 };
        const MULTICAST_SUBNET: Subnet<Ipv4Addr> =
            Subnet { network: Ipv4Addr::new([224, 0, 0, 0]), prefix: 4 };
        type Addr = Ipv4Addr;
    }

    impl Ipv4 {
        /// The global broadcast address.
        ///
        /// This address is considered to be a broadcast address on all networks
        /// regardless of subnet address. This is distinct from the
        /// subnet-specific broadcast address (e.g., 192.168.255.255 on the
        /// subnet 192.168.0.0/16).
        pub const GLOBAL_BROADCAST_ADDRESS: Ipv4Addr = Ipv4Addr::new([255, 255, 255, 255]);

        /// The Class E subnet.
        ///
        /// The Class E subnet is meant for experimental purposes. We should
        /// never receive or emit packets with Class E addresses.
        pub const CLASS_E_SUBNET: Subnet<Ipv4Addr> =
            Subnet { network: Ipv4Addr::new([240, 0, 0, 0]), prefix: 4 };
    }

    /// IPv6.
    ///
    /// `Ipv6` implements `Ip` for IPv6.
    #[derive(Debug, Default)]
    pub struct Ipv6;

    impl Ip for Ipv6 {
        const VERSION: IpVersion = IpVersion::V6;
        const UNSPECIFIED_ADDRESS: Ipv6Addr = Ipv6Addr::new([0; 16]);
        const LOOPBACK_ADDRESS: Ipv6Addr =
            Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);
        const LOOPBACK_SUBNET: Subnet<Ipv6Addr> = Subnet {
            network: Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]),
            prefix: 128,
        };
        const MULTICAST_SUBNET: Subnet<Ipv6Addr> = Subnet {
            network: Ipv6Addr::new([0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]),
            prefix: 8,
        };
        type Addr = Ipv6Addr;
    }

    impl Ipv6 {
        /// The IPv6 All Nodes multicast address in link-local scope,
        /// as defined in [RFC 4291 section 2.7.1].
        ///
        /// [RFC 4291 section 2.7.1]: https://tools.ietf.org/html/rfc4291#section-2.7.1
        pub(crate) const ALL_NODES_LINK_LOCAL_ADDRESS: Ipv6Addr =
            Ipv6Addr::new([0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);

        /// The IPv6 All Routers multicast address in link-local scope,
        /// as defined in [RFC 4291 section 2.7.1].
        ///
        /// [RFC 4291 section 2.7.1]: https://tools.ietf.org/html/rfc4291#section-2.7.1
        pub(crate) const ALL_ROUTERS_LINK_LOCAL_ADDRESS: Ipv6Addr =
            Ipv6Addr::new([0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2]);
    }

    /// The `IpAddress` trait which is part of the core's external API.
    ///
    /// This module exists to hold the external `IpAddress` trait. It is
    /// necessary because that trait and the internal one have the same name, so
    /// they cannot be defined in the same module.
    pub(crate) mod ext {
        /// An IP address.
        ///
        /// `IpAddress` is implemented by the concrete types `Ipv4Addr` and
        /// `Ipv6Addr`, and is used as a trait bound wherever a generic IP
        /// address of either version is needed, such as in `Subnet` and
        /// `AddrSubnet`.
        pub trait IpAddress: super::IpAddress {}

        impl<I: super::IpAddress> IpAddress for I {}
    }

    /// An IPv4 or IPv6 address.
    pub trait IpAddress
    where
        Self:
            Sized + Eq + PartialEq + Hash + Copy + Display + Debug + self::sealed::Sealed + Default,
    {
        /// The number of bytes in an address of this type.
        ///
        /// 4 for IPv4 and 16 for IPv6.
        const BYTES: u8;

        /// The IP version type of this address.
        ///
        /// `Ipv4` for `Ipv4Addr` and `Ipv6` for `Ipv6Addr`.
        type Version: Ip<Addr = Self>;

        /// Get the underlying bytes of the address.
        fn bytes(&self) -> &[u8];

        /// Mask off the top bits of the address.
        ///
        /// Return a copy of `self` where all but the top `bits` bits are set to
        /// 0.
        ///
        /// # Panics
        ///
        /// `mask` panics if `bits` is out of range - if it is greater than 32
        /// for IPv4 or greater than 128 for IPv6.
        fn mask(&self, bits: u8) -> Self;

        /// Convert a statically-typed IP address into a dynamically-typed one.
        fn into_ip_addr(self) -> IpAddr;

        /// Is this the unspecified address?
        ///
        /// `is_unspecified` returns `true` if this address is equal to
        /// `Self::Version::UNSPECIFIED_ADDRESS`.
        fn is_unspecified(&self) -> bool {
            *self == Self::Version::UNSPECIFIED_ADDRESS
        }

        /// Is this a loopback address?
        ///
        /// `is_loopback` returns `true` if this address is a member of the
        /// loopback subnet.
        fn is_loopback(&self) -> bool {
            Self::Version::LOOPBACK_SUBNET.contains(*self)
        }

        /// Is this a multicast address?
        ///
        /// `is_multicast` returns `true` if this address is a member of the
        /// multicast subnet.
        fn is_multicast(&self) -> bool {
            Self::Version::MULTICAST_SUBNET.contains(*self)
        }

        // TODO(joshlf): Is this the right naming convention?

        /// Invoke a function on this address if it is an `Ipv4Address` or
        /// return `default` if it is an `Ipv6Address`.
        fn with_v4<O, F: Fn(Ipv4Addr) -> O>(&self, f: F, default: O) -> O;

        /// Invoke a function on this address if it is an `Ipv6Address` or
        /// return `default` if it is an `Ipv4Address`.
        fn with_v6<O, F: Fn(Ipv6Addr) -> O>(&self, f: F, default: O) -> O;
    }

    /// An IPv4 address.
    #[derive(Copy, Clone, Default, PartialEq, Eq, Hash, FromBytes, AsBytes, Unaligned)]
    #[repr(transparent)]
    pub struct Ipv4Addr([u8; 4]);

    impl Ipv4Addr {
        /// Create a new IPv4 address.
        pub(crate) const fn new(bytes: [u8; 4]) -> Self {
            Ipv4Addr(bytes)
        }

        /// Get the bytes of the IPv4 address.
        pub const fn ipv4_bytes(self) -> [u8; 4] {
            self.0
        }

        /// Is this the global broadcast address?
        ///
        /// `is_global_broadcast` is a shorthand for comparing against
        /// `Ipv4::GLOBAL_BROADCAST_ADDRESS`.
        pub(crate) fn is_global_broadcast(self) -> bool {
            self == Ipv4::GLOBAL_BROADCAST_ADDRESS
        }

        /// Is this a Class E address?
        ///
        /// `is_class_e` is a shorthand for checking membership in
        /// `Ipv4::CLASS_E_SUBNET`.
        pub(crate) fn is_class_e(self) -> bool {
            Ipv4::CLASS_E_SUBNET.contains(self)
        }
    }

    impl IpAddress for Ipv4Addr {
        const BYTES: u8 = 4;

        type Version = Ipv4;

        fn mask(&self, bits: u8) -> Self {
            assert!(bits <= 32);
            if bits == 0 {
                // shifting left by the size of the value is undefined
                Ipv4Addr([0; 4])
            } else {
                let mask = <u32>::max_value() << (32 - bits);
                let masked = NetworkEndian::read_u32(&self.0) & mask;
                let mut ret = Ipv4Addr::default();
                NetworkEndian::write_u32(&mut ret.0, masked);
                ret
            }
        }

        fn bytes(&self) -> &[u8] {
            &self.0
        }

        fn into_ip_addr(self) -> IpAddr {
            IpAddr::V4(self)
        }

        fn with_v4<O, F: Fn(Ipv4Addr) -> O>(&self, f: F, default: O) -> O {
            f(*self)
        }

        fn with_v6<O, F: Fn(Ipv6Addr) -> O>(&self, f: F, default: O) -> O {
            default
        }
    }

    impl From<net::Ipv4Addr> for Ipv4Addr {
        fn from(ip: net::Ipv4Addr) -> Self {
            Ipv4Addr::new(ip.octets())
        }
    }

    impl Display for Ipv4Addr {
        fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
            write!(f, "{}.{}.{}.{}", self.0[0], self.0[1], self.0[2], self.0[3])
        }
    }

    impl Debug for Ipv4Addr {
        fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
            Display::fmt(self, f)
        }
    }

    /// An IPv6 address.
    #[derive(Copy, Clone, Default, PartialEq, Eq, Hash, FromBytes, AsBytes, Unaligned)]
    #[repr(transparent)]
    pub struct Ipv6Addr([u8; 16]);

    impl Ipv6Addr {
        /// Create a new IPv6 address.
        pub(crate) const fn new(bytes: [u8; 16]) -> Self {
            Ipv6Addr(bytes)
        }

        /// Get the bytes of the IPv6 address.
        pub const fn ipv6_bytes(&self) -> [u8; 16] {
            self.0
        }

        /// Converts this `Ipv6Addr` to the IPv6 Solicited-Node Address, used in
        /// Neighbor Discovery. Defined in [RFC 4291 section 2.7.1].
        ///
        /// [RFC 4291 section 2.7.1]: https://tools.ietf.org/html/rfc4291#section-2.7.1
        pub(crate) const fn to_solicited_node_address(&self) -> Self {
            // TODO(brunodalbo) benchmark this generation and evaluate if using
            //  bit operations with u128 could be faster. This is very likely
            //  going to be on a hot path.
            Self::new([
                0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xff, self.0[13], self.0[14],
                self.0[15],
            ])
        }

        /// Checks if packages addressed to the IPv6 address in `dst` should
        /// be delivered to hosts with this IP.
        ///
        /// Checks if `dst` equals to `self` or the Solicited Node Address
        /// version of `self`.
        pub(crate) fn destination_matches(&self, dst: &Self) -> bool {
            self == dst || self.to_solicited_node_address() == *dst
        }

        /// Checks whether `self` is a link local IPv6 address, as defined
        /// in [RFC 4291 section 2.5.6].
        ///
        /// [RFC 4291 section 2.5.6]: https://tools.ietf.org/html/rfc4291#section-2.5.6
        pub(crate) fn is_linklocal(&self) -> bool {
            self.0[0] == 0xFE && self.0[1] & 0xC0 == 0x80
        }

        /// Checks whether `self` is a valid unicast address.
        ///
        /// A valid unicast address is any unicast address that can be bound to
        /// an interface (Not the unspecified or loopback addresses).
        pub(crate) fn is_valid_unicast(&self) -> bool {
            !(self.is_loopback() || self.is_unspecified() || self.is_multicast())
        }
    }

    impl IpAddress for Ipv6Addr {
        const BYTES: u8 = 16;

        type Version = Ipv6;

        fn mask(&self, bits: u8) -> Self {
            assert!(bits <= 128);
            if bits == 0 {
                // shifting left by the size of the value is undefined
                Ipv6Addr([0; 16])
            } else {
                let mask = <u128>::max_value() << (128 - bits);
                let masked = NetworkEndian::read_u128(&self.0) & mask;
                let mut ret = Ipv6Addr::default();
                NetworkEndian::write_u128(&mut ret.0, masked);
                ret
            }
        }

        fn bytes(&self) -> &[u8] {
            &self.0
        }

        fn into_ip_addr(self) -> IpAddr {
            IpAddr::V6(self)
        }

        fn with_v4<O, F: Fn(Ipv4Addr) -> O>(&self, f: F, default: O) -> O {
            default
        }

        fn with_v6<O, F: Fn(Ipv6Addr) -> O>(&self, f: F, default: O) -> O {
            f(*self)
        }
    }

    impl From<net::Ipv6Addr> for Ipv6Addr {
        fn from(ip: net::Ipv6Addr) -> Self {
            Ipv6Addr::new(ip.octets())
        }
    }

    impl Display for Ipv6Addr {
        fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
            let to_u16 = |idx| NetworkEndian::read_u16(&self.0[idx..idx + 2]);
            Display::fmt(
                &net::Ipv6Addr::new(
                    to_u16(0),
                    to_u16(2),
                    to_u16(4),
                    to_u16(6),
                    to_u16(8),
                    to_u16(10),
                    to_u16(12),
                    to_u16(14),
                ),
                f,
            )
        }
    }

    impl Debug for Ipv6Addr {
        fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
            Display::fmt(self, f)
        }
    }

    /// An IP subnet.
    ///
    /// `Subnet` is a combination of an IP network address and a prefix length.
    #[derive(Copy, Clone, Eq, PartialEq)]
    pub struct Subnet<A> {
        // invariant: only contains prefix bits
        network: A,
        prefix: u8,
    }

    // TODO(joshlf): Currently, we need a separate new_unchecked because trait
    // bounds other than Sized are not supported in const fns. Once that
    // restriction is lifted, we can make new a const fn.

    impl<A> Subnet<A> {
        /// Create a new subnet without enforcing correctness.
        ///
        /// Unlike `new`, `new_unchecked` does not validate that `prefix` is in
        /// the proper range, and does not check that `network` has only the top
        /// `prefix` bits set. It is up to the caller to guarantee that `prefix`
        /// is in the proper range, and that none of the bits of `network`
        /// beyond the prefix are set.
        pub(crate) const unsafe fn new_unchecked(network: A, prefix: u8) -> Subnet<A> {
            Subnet { network, prefix }
        }
    }

    impl<A: ext::IpAddress> Subnet<A> {
        /// Create a new subnet.
        ///
        /// `new` creates a new subnet with the given network address and prefix
        /// length. It returns `None` if `prefix` is longer than the number of
        /// bits in this type of IP address (32 for IPv4 and 128 for IPv6) or if
        /// any of the lower bits (beyond the first `prefix` bits) are set in
        /// `network`.
        pub(crate) fn new(network: A, prefix: u8) -> Option<Subnet<A>> {
            // TODO(joshlf): Is there a more efficient way we can perform this
            // check? Do we care?
            if prefix > A::BYTES * 8 || network != network.mask(prefix) {
                return None;
            }
            Some(Subnet { network, prefix })
        }

        /// Get the network address component of this subnet.
        ///
        /// `network` returns the network address component of this subnet. Any
        /// bits beyond the prefix will be zero.
        pub fn network(&self) -> A {
            self.network
        }

        /// Get the prefix length component of this subnet.
        pub fn prefix(&self) -> u8 {
            self.prefix
        }

        /// Test whether an address is in this subnet.
        ///
        /// Test whether `address` is in this subnet by testing whether the
        /// prefix bits match the prefix bits of the subnet's network address.
        /// This is equivalent to `subnet.network() ==
        /// address.mask(subnet.prefix())`.
        pub(crate) fn contains(&self, address: A) -> bool {
            self.network == address.mask(self.prefix)
        }
    }

    impl Subnet<Ipv4Addr> {
        /// Get the broadcast address in this subnet.
        pub(crate) fn broadcast(self) -> Ipv4Addr {
            if self.prefix == 32 {
                // shifting right by the size of the value is undefined
                self.network
            } else {
                let mask = <u32>::max_value() >> self.prefix;
                let masked = NetworkEndian::read_u32(&self.network.0) | mask;
                let mut ret = Ipv4Addr::default();
                NetworkEndian::write_u32(&mut ret.0, masked);
                ret
            }
        }
    }

    impl<A: ext::IpAddress> Display for Subnet<A> {
        fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
            write!(f, "{}/{}", self.network, self.prefix)
        }
    }

    impl<A: ext::IpAddress> Debug for Subnet<A> {
        fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
            write!(f, "{}/{}", self.network, self.prefix)
        }
    }

    /// An IPv4 subnet or an IPv6 subnet.
    ///
    /// `SubnetEither` is an enum of `Subnet<Ipv4Addr>` and `Subnet<Ipv6Addr>`.
    #[allow(missing_docs)]
    #[derive(Copy, Clone, Eq, PartialEq, Debug)]
    pub enum SubnetEither {
        V4(Subnet<Ipv4Addr>),
        V6(Subnet<Ipv6Addr>),
    }

    impl SubnetEither {
        /// Create a new subnet.
        ///
        /// `new` creates a new subnet with the given network address and prefix
        /// length. It returns `None` if `prefix` is longer than the number of
        /// bits in this type of IP address (32 for IPv4 and 128 for IPv6) or if
        /// any of the lower bits (beyond the first `prefix` bits) are set in
        /// `network`.
        pub fn new(network: IpAddr, prefix: u8) -> Option<SubnetEither> {
            Some(match network {
                IpAddr::V4(network) => SubnetEither::V4(Subnet::new(network, prefix)?),
                IpAddr::V6(network) => SubnetEither::V6(Subnet::new(network, prefix)?),
            })
        }
    }

    impl<A: ext::IpAddress> From<Subnet<A>> for SubnetEither {
        default fn from(_addr: Subnet<A>) -> SubnetEither {
            unreachable!()
        }
    }

    impl From<Subnet<Ipv4Addr>> for SubnetEither {
        fn from(subnet: Subnet<Ipv4Addr>) -> SubnetEither {
            SubnetEither::V4(subnet)
        }
    }

    impl From<Subnet<Ipv6Addr>> for SubnetEither {
        fn from(subnet: Subnet<Ipv6Addr>) -> SubnetEither {
            SubnetEither::V6(subnet)
        }
    }

    /// The destination for forwarding a packet.
    ///
    /// `EntryDest` can either be a device or another network address.
    #[allow(missing_docs)]
    #[derive(Copy, Clone, Eq, PartialEq)]
    pub enum EntryDest<A> {
        Local { device: DeviceId },
        Remote { next_hop: A },
    }

    /// A forwarding entry.
    ///
    /// `Entry` is a `Subnet` paired with an `EntryDest`.
    #[derive(Copy, Clone, Eq, PartialEq)]
    pub struct Entry<A> {
        pub subnet: Subnet<A>,
        pub dest: EntryDest<A>,
    }

    /// An IPv4 forwarding entry or an IPv6 forwarding entry.
    #[allow(missing_docs)]
    #[derive(Copy, Clone, Eq, PartialEq)]
    pub enum EntryEither {
        V4(Entry<Ipv4Addr>),
        V6(Entry<Ipv6Addr>),
    }

    impl From<Entry<Ipv4Addr>> for EntryEither {
        fn from(entry: Entry<Ipv4Addr>) -> EntryEither {
            EntryEither::V4(entry)
        }
    }

    impl From<Entry<Ipv6Addr>> for EntryEither {
        fn from(entry: Entry<Ipv6Addr>) -> EntryEither {
            EntryEither::V6(entry)
        }
    }

    /// An address and that address' subnet.
    ///
    /// An `AddrSubnet` is a pair of an address and a subnet which maintains the
    /// invariant that the address is guaranteed to be in the subnet.
    #[derive(Copy, Clone, Eq, PartialEq)]
    pub struct AddrSubnet<A> {
        addr: A,
        subnet: Subnet<A>,
    }

    impl<A: ext::IpAddress> AddrSubnet<A> {
        /// Create a new `AddrSubnet`.
        ///
        /// `new` creates a new `AddrSubnet` with the given address and prefix
        /// length. The network address of the subnet is taken to be the first
        /// `prefix` bits of the address. It returns `None` if `prefix` is
        /// longer than the number of bits in this type of IP address (32 for
        /// IPv4 and 128 for IPv6).
        pub(crate) fn new(addr: A, prefix: u8) -> Option<AddrSubnet<A>> {
            if prefix > A::BYTES * 8 {
                return None;
            }
            Some(AddrSubnet { addr, subnet: Subnet { network: addr.mask(prefix), prefix } })
        }

        /// Get the address.
        pub(crate) fn addr(&self) -> A {
            self.addr
        }

        /// Get the subnet.
        pub(crate) fn subnet(&self) -> Subnet<A> {
            self.subnet
        }

        /// Consume the `AddrSubnet` and return the address.
        pub(crate) fn into_addr(self) -> A {
            self.addr
        }

        /// Consume the `AddrSubnet` and return the subnet.
        pub(crate) fn into_subnet(self) -> Subnet<A> {
            self.subnet
        }

        /// Consume the `AddrSubnet` and return the address and subnet
        /// individually.
        pub(crate) fn into_addr_subnet(self) -> (A, Subnet<A>) {
            (self.addr, self.subnet)
        }
    }

    /// An address and that address' subnet, either IPv4 or IPv6.
    ///
    /// `AddrSubnetEither` is an enum of `AddrSubnet<Ipv4Addr>` and
    /// `AddrSubnet<Ipv6Addr>`.
    #[allow(missing_docs)]
    #[derive(Copy, Clone)]
    pub enum AddrSubnetEither {
        V4(AddrSubnet<Ipv4Addr>),
        V6(AddrSubnet<Ipv6Addr>),
    }

    impl AddrSubnetEither {
        /// Create a new `AddrSubnetEither`.
        ///
        /// `new` creates a new `AddrSubnetEither` with the given address and
        /// prefix length. The network address of the subnet is taken to be the
        /// first `prefix` bits of the address. It returns `None` if `prefix` is
        /// longer than the number of bits in this type of IP address (32 for
        /// IPv4 and 128 for IPv6).
        pub fn new(addr: IpAddr, prefix: u8) -> Option<AddrSubnetEither> {
            Some(match addr {
                IpAddr::V4(addr) => AddrSubnetEither::V4(AddrSubnet::new(addr, prefix)?),
                IpAddr::V6(addr) => AddrSubnetEither::V6(AddrSubnet::new(addr, prefix)?),
            })
        }
    }

    /// An IP protocol or next header number.
    ///
    /// For IPv4, this is the protocol number. For IPv6, this is the next header
    /// number.
    #[allow(missing_docs)]
    #[derive(Copy, Clone, Hash, Eq, PartialEq)]
    pub(crate) enum IpProto {
        Icmp,
        Igmp,
        Tcp,
        Udp,
        Icmpv6,
        Other(u8),
    }

    impl IpProto {
        const ICMP: u8 = 1;
        const IGMP: u8 = 2;
        const TCP: u8 = 6;
        const UDP: u8 = 17;
        const ICMPV6: u8 = 58;
    }

    impl From<u8> for IpProto {
        fn from(u: u8) -> IpProto {
            match u {
                Self::ICMP => IpProto::Icmp,
                Self::IGMP => IpProto::Igmp,
                Self::TCP => IpProto::Tcp,
                Self::UDP => IpProto::Udp,
                Self::ICMPV6 => IpProto::Icmpv6,
                u => IpProto::Other(u),
            }
        }
    }

    impl Into<u8> for IpProto {
        fn into(self) -> u8 {
            match self {
                IpProto::Icmp => Self::ICMP,
                IpProto::Igmp => Self::IGMP,
                IpProto::Tcp => Self::TCP,
                IpProto::Udp => Self::UDP,
                IpProto::Icmpv6 => Self::ICMPV6,
                IpProto::Other(u) => u,
            }
        }
    }

    impl Display for IpProto {
        fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
            write!(
                f,
                "{}",
                match self {
                    IpProto::Icmp => "ICMP",
                    IpProto::Igmp => "IGMP",
                    IpProto::Tcp => "TCP",
                    IpProto::Udp => "UDP",
                    IpProto::Icmpv6 => "ICMPv6",
                    IpProto::Other(u) => return write!(f, "IP protocol {}", u),
                }
            )
        }
    }

    impl Debug for IpProto {
        fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
            Display::fmt(self, f)
        }
    }

    /// An extension trait to the `Ip` trait adding an associated `Packet` type.
    ///
    /// `IpExt` extends the `Ip` trait, adding an associated `Packet` type. It
    /// cannot be part of the `Ip` trait because it requires a `B: ByteSlice`
    /// parameter (due to the requirements of `packet::ParsablePacket`).
    pub(crate) trait IpExt<B: ByteSlice>: Ip {
        type Packet: IpPacket<B, Self, Builder = Self::PacketBuilder>;
        type PacketBuilder: IpPacketBuilder<Self>;
    }

    // NOTE(joshlf): We know that this is safe because we seal the Ip trait to
    // only be implemented by Ipv4 and Ipv6.
    impl<B: ByteSlice, I: Ip> IpExt<B> for I {
        default type Packet = Never;
        default type PacketBuilder = Never;
    }

    impl<B: ByteSlice> IpExt<B> for Ipv4 {
        type Packet = Ipv4Packet<B>;
        type PacketBuilder = Ipv4PacketBuilder;
    }

    impl<B: ByteSlice> IpExt<B> for Ipv6 {
        type Packet = Ipv6Packet<B>;
        type PacketBuilder = Ipv6PacketBuilder;
    }

    /// An IPv4 or IPv6 packet.
    ///
    /// `IpPacket` is implemented by `Ipv4Packet` and `Ipv6Packet`.
    pub(crate) trait IpPacket<B: ByteSlice, I: Ip>:
        Sized + Debug + ParsablePacket<B, (), Error = ParseError>
    {
        /// A builder for this packet type.
        type Builder: IpPacketBuilder<I>;

        /// The source IP address.
        fn src_ip(&self) -> I::Addr;

        /// The destination IP address.
        fn dst_ip(&self) -> I::Addr;

        /// The protocol (IPv4) or next header (IPv6) field.
        fn proto(&self) -> IpProto;

        /// The Time to Live (TTL).
        fn ttl(&self) -> u8;

        /// Set the Time to Live (TTL).
        ///
        /// `set_ttl` updates the packet's TTL in place.
        fn set_ttl(&mut self, ttl: u8)
        where
            B: ByteSliceMut;
    }

    impl<B: ByteSlice> IpPacket<B, Ipv4> for Ipv4Packet<B> {
        type Builder = Ipv4PacketBuilder;

        fn src_ip(&self) -> Ipv4Addr {
            Ipv4Packet::src_ip(self)
        }
        fn dst_ip(&self) -> Ipv4Addr {
            Ipv4Packet::dst_ip(self)
        }
        fn proto(&self) -> IpProto {
            Ipv4Packet::proto(self)
        }
        fn ttl(&self) -> u8 {
            Ipv4Packet::ttl(self)
        }
        fn set_ttl(&mut self, ttl: u8)
        where
            B: ByteSliceMut,
        {
            Ipv4Packet::set_ttl(self, ttl)
        }
    }

    impl<B: ByteSlice> IpPacket<B, Ipv6> for Ipv6Packet<B> {
        type Builder = Ipv6PacketBuilder;

        fn src_ip(&self) -> Ipv6Addr {
            Ipv6Packet::src_ip(self)
        }
        fn dst_ip(&self) -> Ipv6Addr {
            Ipv6Packet::dst_ip(self)
        }
        fn proto(&self) -> IpProto {
            Ipv6Packet::next_header(self)
        }
        fn ttl(&self) -> u8 {
            Ipv6Packet::hop_limit(self)
        }
        fn set_ttl(&mut self, ttl: u8)
        where
            B: ByteSliceMut,
        {
            Ipv6Packet::set_hop_limit(self, ttl)
        }
    }

    /// A builder for IP packets.
    ///
    /// `IpPacketBuilder` is implemented by `Ipv4PacketBuilder` and
    /// `Ipv6PacketBuilder`.
    pub(crate) trait IpPacketBuilder<I: Ip>: PacketBuilder {
        fn new(src_ip: I::Addr, dst_ip: I::Addr, ttl: u8, proto: IpProto) -> Self;
    }

    impl IpPacketBuilder<Ipv4> for Ipv4PacketBuilder {
        fn new(src_ip: Ipv4Addr, dst_ip: Ipv4Addr, ttl: u8, proto: IpProto) -> Ipv4PacketBuilder {
            Ipv4PacketBuilder::new(src_ip, dst_ip, ttl, proto)
        }
    }

    impl IpPacketBuilder<Ipv6> for Ipv6PacketBuilder {
        fn new(src_ip: Ipv6Addr, dst_ip: Ipv6Addr, ttl: u8, proto: IpProto) -> Ipv6PacketBuilder {
            Ipv6PacketBuilder::new(src_ip, dst_ip, ttl, proto)
        }
    }

    /// An IPv4 header option.
    ///
    /// An IPv4 header option comprises metadata about the option (which is
    /// stored in the kind byte) and the option itself. Note that all
    /// kind-byte-only options are handled by the utilities in
    /// `wire::util::options`, so this type only supports options with
    /// variable-length data.
    ///
    /// See [Wikipedia] or [RFC 791] for more details.
    ///
    /// [Wikipedia]: https://en.wikipedia.org/wiki/IPv4#Options [RFC 791]:
    /// https://tools.ietf.org/html/rfc791#page-15
    pub(crate) struct Ipv4Option<'a> {
        /// Whether this option needs to be copied into all fragments of a fragmented packet.
        pub(crate) copied: bool,
        // TODO(joshlf): include "Option Class"?
        /// The variable-length option data.
        pub(crate) data: Ipv4OptionData<'a>,
    }

    /// The data associated with an IPv4 header option.
    ///
    /// `Ipv4OptionData` represents the variable-length data field of an IPv4
    /// header option.
    #[allow(missing_docs)]
    pub(crate) enum Ipv4OptionData<'a> {
        // The maximum header length is 60 bytes, and the fixed-length header is
        // 20 bytes, so there are 40 bytes for the options. That leaves a
        // maximum options size of 1 kind byte + 1 length byte + 38 data bytes.
        /// Data for an unrecognized option kind.
        ///
        /// Any unrecognized option kind will have its data parsed using this
        /// variant. This allows code to copy unrecognized options into packets
        /// when forwarding.
        ///
        /// `data`'s length is in the range [0, 38].
        Unrecognized { kind: u8, len: u8, data: &'a [u8] },
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use crate::testutil::set_logger_for_test;

        #[test]
        fn test_subnet_new() {
            Subnet::new(Ipv4Addr::new([255, 255, 255, 255]), 32).unwrap();
            // Prefix exceeds 32 bits
            assert_eq!(Subnet::new(Ipv4Addr::new([255, 255, 0, 0]), 33), None);
            // Network address has more than top 8 bits set
            assert_eq!(Subnet::new(Ipv4Addr::new([255, 255, 0, 0]), 8), None);

            AddrSubnet::new(Ipv4Addr::new([255, 255, 255, 255]), 32).unwrap();
            // Prefix exceeds 32 bits (use assert, not assert_eq, because
            // AddrSubnet doesn't impl Debug)
            assert!(AddrSubnet::new(Ipv4Addr::new([255, 255, 255, 255]), 33) == None);
        }

        macro_rules! add_mask_test {
            ($name:ident, $addr:ident, $from_ip:expr => {
                $($mask:expr => $to_ip:expr),*
            }) => {
                #[test]
                fn $name() {
                    let from = $addr::new($from_ip);
                    $(assert_eq!(from.mask($mask), $addr::new($to_ip), "(`{}`.mask({}))", from, $mask);)*
                }
            };
            ($name:ident, $addr:ident, $from_ip:expr => {
                $($mask:expr => $to_ip:expr),*,
            }) => {
                add_mask_test!($name, $addr, $from_ip => { $($mask => $to_ip),* });
            };
        }

        add_mask_test!(v4_full_mask, Ipv4Addr, [255, 254, 253, 252] => {
            32 => [255, 254, 253, 252],
            28 => [255, 254, 253, 240],
            24 => [255, 254, 253, 0],
            20 => [255, 254, 240, 0],
            16 => [255, 254, 0,   0],
            12 => [255, 240, 0,   0],
            8  => [255, 0,   0,   0],
            4  => [240, 0,   0,   0],
            0  => [0,   0,   0,   0],
        });

        add_mask_test!(v6_full_mask, Ipv6Addr,
            [0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7, 0xF6, 0xF5, 0xF4, 0xF3, 0xF2, 0xF1, 0xF0] => {
                128 => [0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7, 0xF6, 0xF5, 0xF4, 0xF3, 0xF2, 0xF1, 0xF0],
                112 => [0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7, 0xF6, 0xF5, 0xF4, 0xF3, 0xF2, 0x00, 0x00],
                96  => [0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7, 0xF6, 0xF5, 0xF4, 0x00, 0x00, 0x00, 0x00],
                80  => [0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7, 0xF6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
                64  => [0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
                48  => [0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
                32  => [0xFF, 0xFE, 0xFD, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
                16  => [0xFF, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
                8   => [0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
                0   => [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
            }
        );

        #[test]
        fn test_ipv6_solicited_node() {
            let addr = Ipv6Addr::new([
                0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x52, 0xe5, 0x49, 0xff, 0xfe, 0xb5, 0x5a, 0xa0,
            ]);
            let solicited = Ipv6Addr::new([
                0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xff, 0xb5, 0x5a, 0xa0,
            ]);
            assert_eq!(addr.to_solicited_node_address(), solicited);
            assert!(addr.destination_matches(&addr));
            assert!(addr.destination_matches(&solicited));
        }

        #[test]
        fn test_ipv6_address_types() {
            set_logger_for_test();
            assert!(Ipv6Addr::new([0; 16]).is_unspecified());
            assert!(Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]).is_loopback());
            let link_local = Ipv6Addr::new([
                0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x52, 0xe5, 0x49, 0xff, 0xfe, 0xb5, 0x5a, 0xa0,
            ]);
            assert!(link_local.is_linklocal());
            assert!(link_local.is_valid_unicast());
            assert!(link_local.to_solicited_node_address().is_multicast());
            let global_unicast = Ipv6Addr::new([
                0x00, 0x80, 0, 0, 0, 0, 0, 0, 0x52, 0xe5, 0x49, 0xff, 0xfe, 0xb5, 0x5a, 0xa0,
            ]);
            assert!(global_unicast.is_valid_unicast());
            assert!(global_unicast.to_solicited_node_address().is_multicast());

            let multi = Ipv6Addr::new([
                0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xff, 0xb5, 0x5a, 0xa0,
            ]);
            assert!(multi.is_multicast());
            assert!(!multi.is_valid_unicast());
        }
    }
}
