// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO: Edit this doc comment (it's copy+pasted from the Netstack3 core)

//! IP protocol types.
//!
//! We provide the following types:
//!
//! # IP Versions
//!
//! * [`IpVersion`]: An enum representing IPv4 or IPv6
//! * [`Ip`]: An IP version trait that can be used write code which is generic
//!   over both versions of the IP protocol
//!
//! # IP Addresses
//!
//! * [`Ipv4Addr`]: A concrete IPv4 address
//! * [`Ipv6Addr`]: A concrete IPv6 address
//! * [`IpAddr`]: An enum representing either a v4 or v6 address.
//! * [`IpAddress`]: An IP address trait that can be used to write code which is
//!   generic over both types of IP address
//!
//! # Subnets
//!
//! * [`Subnet`]: A v4 or v6 subnet, as specified by the type parameter.
//! * [`SubnetEither`]: An enum of either a v4 subnet or a v6 subnet.
//!
//! # Address + Subnet Pairs:
//!
//! * [`AddrSubnet`]: A v4 or v6 subnet + address pair, as specified by the type
//!   parameter.
//! * [`AddrSubnetEither`]: An enum of either a v4 or a v6 subnet + address
//!   pair.
//!
//! [`IpVersion`]: crate::ip::IpVersion
//! [`Ip`]: crate::ip::Ip
//! [`Ipv4Addr`]: crate::ip::Ipv4Addr
//! [`Ipv6Addr`]: crate::ip::Ipv6Addr
//! [`IpAddr`]: crate::ip::IpAddr
//! [`IpAddress`]: crate::ip::IpAddress
//! [`Subnet`]: crate::ip::Subnet
//! [`SubnetEither`]: crate::ip::SubnetEither
//! [`AddrSubnet`]: crate::ip::AddrSubnet
//! [`AddrSubnetEither`]: crate::ip::AddrSubnetEither

// TODO(joshlf): Add RFC references for various standards such as the global
// broadcast address or the Class E subnet.

use core::fmt::{self, Debug, Display, Formatter};
use core::hash::Hash;

#[cfg(std)]
use std::net;

use byteorder::{ByteOrder, NetworkEndian};
use zerocopy::{AsBytes, FromBytes, Unaligned};

use crate::{
    sealed, LinkLocalAddr, LinkLocalAddress, MulticastAddr, MulticastAddress, SpecifiedAddr,
    SpecifiedAddress, UnicastAddr, UnicastAddress, Witness,
};

// NOTE on passing by reference vs by value: Clippy advises us to pass IPv4
// addresses by value, and IPv6 addresses by reference. For concrete types, we
// do the right thing. For the IpAddress trait, we use references in order to
// optimize (albeit very slightly) for IPv6 performance.

/// An IP protocol version.
#[allow(missing_docs)]
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub enum IpVersion {
    V4,
    V6,
}

/// An IP address.
///
/// By default, the contained address types are `Ipv4Addr` and `Ipv6Addr`.
/// However, any types can be provided. This is intended to support types like
/// `IpAddr<SpecifiedAddr<Ipv4Addr>, SpecifiedAddr<Ipv6Addr>>`. `From` is
/// implemented to support conversions in both directions between
/// `IpAddr<SpecifiedAddr<Ipv4Addr>, SpecifiedAddr<Ipv6Addr>>` and
/// `SpecifiedAddr<IpAddr>`, and similarly for other witness types.
#[allow(missing_docs)]
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub enum IpAddr<V4 = Ipv4Addr, V6 = Ipv6Addr> {
    V4(V4),
    V6(V6),
}

impl<A: IpAddress> From<A> for IpAddr {
    #[inline]
    fn from(addr: A) -> IpAddr {
        addr.into_ip_addr()
    }
}

#[cfg(std)]
impl From<net::IpAddr> for IpAddr {
    #[inline]
    fn from(addr: net::IpAddr) -> IpAddr {
        match addr {
            net::IpAddr::V4(addr) => IpAddr::V4(addr.into()),
            net::IpAddr::V6(addr) => IpAddr::V6(addr.into()),
        }
    }
}

#[cfg(std)]
impl From<IpAddr> for net::IpAddr {
    fn from(addr: IpAddr) -> net::IpAddr {
        match addr {
            IpAddr::V4(addr) => net::IpAddr::V4(addr.into()),
            IpAddr::V6(addr) => net::IpAddr::V6(addr.into()),
        }
    }
}

impl IpVersion {
    /// The number for this IP protocol version.
    ///
    /// 4 for `V4` and 6 for `V6`.
    #[inline]
    pub fn version_number(self) -> u8 {
        match self {
            IpVersion::V4 => 4,
            IpVersion::V6 => 6,
        }
    }

    /// Is this IPv4?
    #[inline]
    pub fn is_v4(self) -> bool {
        self == IpVersion::V4
    }

    /// Is this IPv6?
    #[inline]
    pub fn is_v6(self) -> bool {
        self == IpVersion::V6
    }
}

/// A trait for IP protocol versions.
///
/// `Ip` encapsulates the details of a version of the IP protocol. It includes
/// the [`IpVersion`] enum (`VERSION`) and an [`IpAddress`] type (`Addr`). It is
/// implemented by [`Ipv4`] and [`Ipv6`]. This trait is sealed, and there are
/// guaranteed to be no other implementors besides these. Code - including
/// unsafe code - may rely on this assumption for its correctness and soundness.
///
/// Note that the implementors of this trait are not meant to be instantiated
/// (in fact, they can't be instantiated). They are only meant to exist at the
/// type level.
pub trait Ip:
    Sized
    + Clone
    + Copy
    + Debug
    + Default
    + Eq
    + Hash
    + Ord
    + PartialEq
    + PartialOrd
    + Send
    + Sync
    + sealed::Sealed
    + 'static
{
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
    /// When sending packets to a loopback interface, this address is used as
    /// the source address. It is an address in the loopback subnet.
    const LOOPBACK_ADDRESS: SpecifiedAddr<Self::Addr>;

    /// The subnet of loopback addresses.
    ///
    /// Addresses in this subnet must not appear outside a host, and may only be
    /// used for loopback interfaces.
    const LOOPBACK_SUBNET: Subnet<Self::Addr>;

    /// The subnet of multicast addresses.
    const MULTICAST_SUBNET: Subnet<Self::Addr>;

    /// The subnet of link-local unicast addresses.
    ///
    /// Note that some multicast addresses are also link-local. In IPv4, these
    /// are contained in the [link-local multicast subnet]. In IPv6, the
    /// link-local multicast addresses are not organized into a single subnet;
    /// instead, whether a multicast IPv6 address is link-local is a function of
    /// its scope.
    ///
    /// [link-local multicast subnet]: Ipv4::LINK_LOCAL_MULTICAST_SUBNET
    const LINK_LOCAL_UNICAST_SUBNET: Subnet<Self::Addr>;

    /// "IPv4" or "IPv6".
    const NAME: &'static str;

    /// The address type for this IP version.
    ///
    /// [`Ipv4Addr`] for IPv4 and [`Ipv6Addr`] for IPv6.
    type Addr: IpAddress<Version = Self>;
}

/// IPv4.
///
/// `Ipv4` implements `Ip` for IPv4.
///
/// Note that this type has no value constructor. It is used purely at the type
/// level. Attempting to construct it by calling `Default::default` will panic.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum Ipv4 {}

impl Default for Ipv4 {
    fn default() -> Ipv4 {
        panic!("Ipv4 default")
    }
}

impl sealed::Sealed for Ipv4 {}

impl Ip for Ipv4 {
    const VERSION: IpVersion = IpVersion::V4;
    const UNSPECIFIED_ADDRESS: Ipv4Addr = Ipv4Addr::new([0, 0, 0, 0]);
    // https://tools.ietf.org/html/rfc5735#section-3
    const LOOPBACK_ADDRESS: SpecifiedAddr<Ipv4Addr> = SpecifiedAddr(Ipv4Addr::new([127, 0, 0, 1]));
    const LOOPBACK_SUBNET: Subnet<Ipv4Addr> =
        Subnet { network: Ipv4Addr::new([127, 0, 0, 0]), prefix: 8 };
    const MULTICAST_SUBNET: Subnet<Ipv4Addr> =
        Subnet { network: Ipv4Addr::new([224, 0, 0, 0]), prefix: 4 };
    /// The subnet of link-local unicast addresses, outlined in [RFC 3927
    /// Section 2.1].
    ///
    /// [RFC 3927 Section 2.1]: https://tools.ietf.org/html/rfc3927#section-2.1
    const LINK_LOCAL_UNICAST_SUBNET: Subnet<Ipv4Addr> =
        Subnet { network: Ipv4Addr::new([169, 254, 0, 0]), prefix: 16 };
    const NAME: &'static str = "IPv4";
    type Addr = Ipv4Addr;
}

impl Ipv4 {
    /// The global broadcast address.
    ///
    /// This address is considered to be a broadcast address on all networks
    /// regardless of subnet address. This is distinct from the subnet-specific
    /// broadcast address (e.g., 192.168.255.255 on the subnet 192.168.0.0/16).
    pub const GLOBAL_BROADCAST_ADDRESS: SpecifiedAddr<Ipv4Addr> =
        SpecifiedAddr(Ipv4Addr::new([255, 255, 255, 255]));

    /// The Class E subnet.
    ///
    /// The Class E subnet is meant for experimental purposes, and should not be
    /// used on the general internet. [RFC 1812 Section 5.3.7] suggests that
    /// routers SHOULD discard packets with a source address in the Class E
    /// subnet.
    ///
    /// [RFC 1812 Section 5.3.7]: https://tools.ietf.org/html/rfc1812#section-5.3.7
    pub const CLASS_E_SUBNET: Subnet<Ipv4Addr> =
        Subnet { network: Ipv4Addr::new([240, 0, 0, 0]), prefix: 4 };

    /// The subnet of link-local multicast addresses, outlined in [RFC 5771
    /// Section 4].
    ///
    /// [RFC 5771 Section 4]: https://tools.ietf.org/html/rfc5771#section-4
    const LINK_LOCAL_MULTICAST_SUBNET: Subnet<Ipv4Addr> =
        Subnet { network: Ipv4Addr::new([169, 254, 0, 0]), prefix: 16 };
}

/// IPv6.
///
/// `Ipv6` implements `Ip` for IPv6.
///
/// Note that this type has no value constructor. It is used purely at the type
/// level. Attempting to construct it by calling `Default::default` will panic.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum Ipv6 {}

impl Default for Ipv6 {
    fn default() -> Ipv6 {
        panic!("Ipv6 default")
    }
}

impl sealed::Sealed for Ipv6 {}

impl Ip for Ipv6 {
    const VERSION: IpVersion = IpVersion::V6;
    const UNSPECIFIED_ADDRESS: Ipv6Addr = Ipv6Addr::new([0; 16]);
    const LOOPBACK_ADDRESS: SpecifiedAddr<Ipv6Addr> =
        SpecifiedAddr(Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]));
    const LOOPBACK_SUBNET: Subnet<Ipv6Addr> = Subnet {
        network: Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]),
        prefix: 128,
    };
    const MULTICAST_SUBNET: Subnet<Ipv6Addr> = Subnet {
        network: Ipv6Addr::new([0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]),
        prefix: 8,
    };
    /// The subnet of link-local unicast addresses, defined in [RFC 4291 Section
    /// 2.5.6].
    ///
    /// Note that multicast addresses can also be link-local. However, there is no
    /// single subnet of link-local multicast addresses. For more details on
    /// link-local multicast addresses, see [RFC 4291 Section 2.7].
    ///
    /// [RFC 4291 Section 2.5.6]: https://tools.ietf.org/html/rfc4291#section-2.5.6
    /// [RFC 4291 Section 2.7]: https://tools.ietf.org/html/rfc4291#section-2.7
    const LINK_LOCAL_UNICAST_SUBNET: Subnet<Ipv6Addr> = Subnet {
        network: Ipv6Addr::new([0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]),
        prefix: 10,
    };
    const NAME: &'static str = "IPv6";
    type Addr = Ipv6Addr;
}

impl Ipv6 {
    /// The IPv6 All Nodes multicast address in link-local scope, as defined in
    /// [RFC 4291 Section 2.7.1].
    ///
    /// [RFC 4291 Section 2.7.1]: https://tools.ietf.org/html/rfc4291#section-2.7.1
    pub const ALL_NODES_LINK_LOCAL_ADDRESS: Ipv6Addr =
        Ipv6Addr::new([0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);

    /// The IPv6 All Routers multicast address in link-local scope, as defined
    /// in [RFC 4291 Section 2.7.1].
    ///
    /// [RFC 4291 Section 2.7.1]: https://tools.ietf.org/html/rfc4291#section-2.7.1
    pub const ALL_ROUTERS_LINK_LOCAL_ADDRESS: Ipv6Addr =
        Ipv6Addr::new([0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2]);
}

/// An IPv4 or IPv6 address.
///
/// `IpAddress` is implemented by [`Ipv4Addr`] and [`Ipv6Addr`]. It is sealed,
/// and there are guaranteed to be no other implementors besides these. Code -
/// including unsafe code - may rely on this assumption for its correctness and
/// soundness.
pub trait IpAddress:
    Sized
    + Eq
    + PartialEq
    + Hash
    + Copy
    + Display
    + Debug
    + Default
    + Sync
    + Send
    + LinkLocalAddress
    + sealed::Sealed
    + 'static
{
    /// The number of bytes in an address of this type.
    ///
    /// 4 for IPv4 and 16 for IPv6.
    const BYTES: u8;

    /// The IP version type of this address.
    ///
    /// `Ipv4` for `Ipv4Addr` and `Ipv6` for `Ipv6Addr`.
    type Version: Ip<Addr = Self>;

    /// Gets the underlying bytes of the address.
    fn bytes(&self) -> &[u8];

    /// Masks off the top bits of the address.
    ///
    /// Returns a copy of `self` where all but the top `bits` bits are set to
    /// 0.
    ///
    /// # Panics
    ///
    /// `mask` panics if `bits` is out of range - if it is greater than 32 for
    /// IPv4 or greater than 128 for IPv6.
    fn mask(&self, bits: u8) -> Self;

    /// Converts a statically-typed IP address into a dynamically-typed one.
    fn into_ip_addr(self) -> IpAddr;

    /// Is this a loopback address?
    ///
    /// `is_loopback` returns `true` if this address is a member of the loopback
    /// subnet.
    #[inline]
    fn is_loopback(&self) -> bool {
        Self::Version::LOOPBACK_SUBNET.contains(self)
    }

    /// Is this a unicast address in the context of the given subnet?
    ///
    /// `is_unicast_in_subnet` returns `true` if this address is none of:
    /// - a multicast address
    /// - the IPv4 global broadcast address
    /// - the IPv4 subnet-specific broadcast address for the given `subnet`
    /// - an IPv4 address whose host bits (those bits following the network
    ///   prefix) are all 0
    /// - the unspecified address
    /// - an IPv4 Class E address
    ///
    /// Note two exceptions to these rules: If `subnet` is an IPv4 /32, then the
    /// single unicast address in the subnet is also technically the subnet
    /// broadcast address. If `subnet` is an IPv4 /31, then both addresses in
    /// that subnet are broadcast addresses. In either case, the "no
    /// subnet-specific broadcast" and "no address with a host part of all
    /// zeroes" rules don't apply. Note further that this exception *doesn't*
    /// apply to the unspecified address, which is never considered a unicast
    /// address regardless of what subnet it's in.
    ///
    /// # RFC Deep Dive
    ///
    /// ## IPv4 addresses ending in zeroes
    ///
    /// In this section, we justify the rule that IPv4 addresses whose host bits
    /// are all 0 are not considered unicast addresses.
    ///
    /// In earlier standards, an IPv4 address whose bits were all 0 after the
    /// network prefix (e.g., 192.168.0.0 in the subnet 192.168.0.0/16) were a
    /// form of "network-prefix-directed" broadcast addresses. Similarly,
    /// 0.0.0.0 was considered a form of "limited broadcast address". These have
    /// since been deprecated (in the case of 0.0.0.0, it is now considered the
    /// "unspecified" address).
    ///
    /// As evidence that this deprecation is official, consider [RFC 1812
    /// Section 5.3.5]. In reference to these types of addresses, it states that
    /// "packets addressed to any of these addresses SHOULD be silently
    /// discarded [by routers]". This not only deprecates them as broadcast
    /// addresses, but also as unicast addresses (after all, unicast addresses
    /// are not particularly useful if packets destined to them are discarded by
    /// routers).
    ///
    /// ## IPv4 /31 and /32 exceptions
    ///
    /// In this section, we justify the exceptions that all addresses in IPv4
    /// /31 and /32 subnets are considered unicast.
    ///
    /// For /31 subnets, the case is easy. [RFC 3021 Section 2.1] states that
    /// both addresses in a /31 subnet "MUST be interpreted as host addresses."
    ///
    /// For /32, the case is a bit more vague. RFC 3021 makes no mention of /32
    /// subnets. However, the same reasoning applies - if an exception is not
    /// made, then there do not exist any host addresses in a /32 subnet. [RFC
    /// 4632 Section 3.1] also vaguely implies this interpretation by referring
    /// to addresses in /32 subnets as "host routes."
    ///
    /// [RFC 1812 Section 5.3.5]: https://tools.ietf.org/html/rfc1812#page-92
    /// [RFC 4632 Section 3.1]: https://tools.ietf.org/html/rfc4632#section-3.1
    fn is_unicast_in_subnet(&self, subnet: &Subnet<Self>) -> bool;

    /// Invokes one function on this address if it is an [`Ipv4Addr`] and
    /// another if it is an [`Ipv6Addr`].
    fn with<O, F4: Fn(&Ipv4Addr) -> O, F6: Fn(&Ipv6Addr) -> O>(&self, v4: F4, v6: F6) -> O;

    /// Invokes a function on this address if it is an [`Ipv4Addr`] or return
    /// `default` if it is an [`Ipv6Addr`].
    fn with_v4<O, F: Fn(&Ipv4Addr) -> O>(&self, f: F, default: O) -> O;

    /// Invokes a function on this address if it is an [`Ipv6Addr`] or return
    /// `default` if it is an [`Ipv4Addr`].
    fn with_v6<O, F: Fn(&Ipv6Addr) -> O>(&self, f: F, default: O) -> O;

    // Functions used to implement internal types. These functions aren't
    // particularly useful to users, but allow us to implement certain
    // specialization-like behavior without actually relying on the unstable
    // `specialization` feature.

    #[doc(hidden)]
    fn into_subnet_either(subnet: Subnet<Self>) -> SubnetEither;
}

impl<A: IpAddress> SpecifiedAddress for A {
    /// Is this an address other than the unspecified address?
    ///
    /// `is_specified` returns true if `self` is not equal to [`A::Version::UNSPECIFIED_ADDRESS`].
    ///
    /// [`A::Version::UNSPECIFIED_ADDRESS`]: crate::ip::Ip::UNSPECIFIED_ADDRESS
    #[inline]
    fn is_specified(&self) -> bool {
        self != &A::Version::UNSPECIFIED_ADDRESS
    }
}

/// Map a method over an `IpAddr`, calling it after matching on the type of IP
/// address.
macro_rules! map_ip_addr {
    ($val:expr, $method:ident) => {
        match $val {
            IpAddr::V4(a) => a.$method(),
            IpAddr::V6(a) => a.$method(),
        }
    };
}

impl SpecifiedAddress for IpAddr {
    /// Is this an address other than the unspecified address?
    ///
    /// `is_specified` returns true if `self` is not equal to
    /// [`Ip::UNSPECIFIED_ADDRESS`] for the IP version of this address.
    #[inline]
    fn is_specified(&self) -> bool {
        map_ip_addr!(self, is_specified)
    }
}

impl<A: IpAddress> MulticastAddress for A {
    /// Is this address in the multicast subnet?
    ///
    /// `is_multicast` returns true if `self` is in
    /// [`A::Version::MULTICAST_SUBNET`].
    ///
    /// [`A::Version::MULTICAST_SUBNET`]: crate::ip::Ip::MULTICAST_SUBNET
    #[inline]
    fn is_multicast(&self) -> bool {
        <A as IpAddress>::Version::MULTICAST_SUBNET.contains(self)
    }
}

impl MulticastAddress for IpAddr {
    /// Is this an address in the multicast subnet?
    ///
    /// `is_multicast` returns true if `self` is in [`Ip::MULTICAST_SUBNET`] for
    /// the IP version of this address.
    #[inline]
    fn is_multicast(&self) -> bool {
        map_ip_addr!(self, is_multicast)
    }
}

impl LinkLocalAddress for Ipv4Addr {
    /// Is this address in the link-local subnet?
    ///
    /// `is_linklocal` returns true if `self` is in
    /// [`Ipv4::LINK_LOCAL_UNICAST_SUBNET`] or
    /// [`Ipv4::LINK_LOCAL_MULTICAST_SUBNET`].
    #[inline]
    fn is_linklocal(&self) -> bool {
        Ipv4::LINK_LOCAL_UNICAST_SUBNET.contains(self)
            || Ipv4::LINK_LOCAL_MULTICAST_SUBNET.contains(self)
    }
}

impl LinkLocalAddress for Ipv6Addr {
    /// Is this address in the link-local subnet?
    ///
    /// `is_linklocal` returns true if `self` is in
    /// [`Ipv6::LINK_LOCAL_UNICAST_SUBNET`] or `self` is a multicast address
    /// whose scope is link-local.
    #[inline]
    fn is_linklocal(&self) -> bool {
        const LINK_LOCAL_SCOPE: u8 = 0x02;
        // TODO(joshlf): Stop doing this manually once we have a general-purpose
        // mechanism for extracting the scope from a multicast address.
        Ipv6::LINK_LOCAL_UNICAST_SUBNET.contains(self)
            || (self.is_multicast() && self.0[1] & 0x0F == LINK_LOCAL_SCOPE)
    }
}

impl LinkLocalAddress for IpAddr {
    /// Is this address link-local?
    #[inline]
    fn is_linklocal(&self) -> bool {
        map_ip_addr!(self, is_linklocal)
    }
}

/// The definition of each trait for `IpAddr` is equal to the definition of that
/// trait for whichever of `Ipv4Addr` and `Ipv6Addr` is actually present in the
/// enum. Thus, we can convert between `$witness<IpvXAddr>`, `$witness<IpAddr>`,
/// and `IpAddr<$witness<Ipv4Addr>, $witness<Ipv6Addr>>` arbitrarily.
macro_rules! impl_from_witness {
    ($witness:ident) => {
        impl_from_witness!($witness, Ipv4Addr);
        impl_from_witness!($witness, Ipv6Addr);

        impl From<IpAddr<$witness<Ipv4Addr>, $witness<Ipv6Addr>>> for $witness<IpAddr> {
            fn from(addr: IpAddr<$witness<Ipv4Addr>, $witness<Ipv6Addr>>) -> $witness<IpAddr> {
                unsafe {
                    $witness::new_unchecked(match addr {
                        IpAddr::V4(addr) => IpAddr::V4(addr.into_addr()),
                        IpAddr::V6(addr) => IpAddr::V6(addr.into_addr()),
                    })
                }
            }
        }
        impl From<$witness<IpAddr>> for IpAddr<$witness<Ipv4Addr>, $witness<Ipv6Addr>> {
            fn from(addr: $witness<IpAddr>) -> IpAddr<$witness<Ipv4Addr>, $witness<Ipv6Addr>> {
                unsafe {
                    match addr.into_addr() {
                        IpAddr::V4(addr) => IpAddr::V4($witness::new_unchecked(addr)),
                        IpAddr::V6(addr) => IpAddr::V6($witness::new_unchecked(addr)),
                    }
                }
            }
        }
    };
    ($witness:ident, $ipaddr:ident) => {
        impl From<$witness<$ipaddr>> for $witness<IpAddr> {
            fn from(addr: $witness<$ipaddr>) -> $witness<IpAddr> {
                unsafe { $witness::new_unchecked(addr.get().into()) }
            }
        }

        impl From<$witness<$ipaddr>> for $ipaddr {
            fn from(addr: $witness<$ipaddr>) -> $ipaddr {
                addr.into_addr()
            }
        }
    };
}

impl_from_witness!(SpecifiedAddr);
impl_from_witness!(MulticastAddr);
impl_from_witness!(LinkLocalAddr);
impl_from_witness!(UnicastAddr, Ipv6Addr);

/// An IPv4 address.
#[derive(Copy, Clone, Default, PartialEq, Eq, Hash, FromBytes, AsBytes, Unaligned)]
#[repr(transparent)]
pub struct Ipv4Addr([u8; 4]);

impl Ipv4Addr {
    /// Creates a new IPv4 address.
    #[inline]
    pub const fn new(bytes: [u8; 4]) -> Self {
        Ipv4Addr(bytes)
    }

    /// Gets the bytes of the IPv4 address.
    #[inline]
    pub const fn ipv4_bytes(self) -> [u8; 4] {
        self.0
    }

    /// Is this the global broadcast address?
    ///
    /// `is_global_broadcast` is a shorthand for comparing against
    /// [`Ipv4::GLOBAL_BROADCAST_ADDRESS`].
    #[inline]
    pub fn is_global_broadcast(self) -> bool {
        self == Ipv4::GLOBAL_BROADCAST_ADDRESS.into_addr()
    }

    /// Is this a Class E address?
    ///
    /// `is_class_e` is a shorthand for checking membership in
    /// [`Ipv4::CLASS_E_SUBNET`].
    #[inline]
    pub fn is_class_e(self) -> bool {
        Ipv4::CLASS_E_SUBNET.contains(&self)
    }
}

impl sealed::Sealed for Ipv4Addr {}

impl IpAddress for Ipv4Addr {
    const BYTES: u8 = 4;

    type Version = Ipv4;

    #[inline]
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

    #[inline]
    fn bytes(&self) -> &[u8] {
        &self.0
    }

    #[inline]
    fn into_ip_addr(self) -> IpAddr {
        IpAddr::V4(self)
    }

    #[inline]
    fn is_unicast_in_subnet(&self, subnet: &Subnet<Self>) -> bool {
        !self.is_multicast()
            && !self.is_global_broadcast()
            // This clause implements the rules that (the subnet broadcast is
            // not unicast AND the address with an all-zeroes host part is not
            // unicast) UNLESS the prefix length is 31 or 32.
            && (subnet.prefix() == 32
                || subnet.prefix() == 31
                || (*self != subnet.broadcast() && *self != subnet.network()))
            && self.is_specified()
            && !self.is_class_e()
    }

    #[inline]
    fn with<O, F4: Fn(&Ipv4Addr) -> O, F6: Fn(&Ipv6Addr) -> O>(&self, v4: F4, _v6: F6) -> O {
        v4(self)
    }

    #[inline]
    fn with_v4<O, F: Fn(&Ipv4Addr) -> O>(&self, f: F, _default: O) -> O {
        f(self)
    }

    #[inline]
    fn with_v6<O, F: Fn(&Ipv6Addr) -> O>(&self, _f: F, default: O) -> O {
        default
    }

    fn into_subnet_either(subnet: Subnet<Ipv4Addr>) -> SubnetEither {
        SubnetEither::V4(subnet)
    }
}

impl From<[u8; 4]> for Ipv4Addr {
    #[inline]
    fn from(bytes: [u8; 4]) -> Ipv4Addr {
        Ipv4Addr(bytes)
    }
}

#[cfg(std)]
impl From<net::Ipv4Addr> for Ipv4Addr {
    #[inline]
    fn from(ip: net::Ipv4Addr) -> Ipv4Addr {
        Ipv4Addr::new(ip.octets())
    }
}

#[cfg(std)]
impl From<Ipv4Addr> for net::Ipv4Addr {
    #[inline]
    fn from(ip: Ipv4Addr) -> net::Ipv4Addr {
        Ipv4Addr::from(ip.0)
    }
}

impl Display for Ipv4Addr {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), fmt::Error> {
        write!(f, "{}.{}.{}.{}", self.0[0], self.0[1], self.0[2], self.0[3])
    }
}

impl Debug for Ipv4Addr {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), fmt::Error> {
        Display::fmt(self, f)
    }
}

/// An IPv6 address.
#[derive(Copy, Clone, Default, PartialEq, Eq, Hash, FromBytes, AsBytes, Unaligned)]
#[repr(transparent)]
pub struct Ipv6Addr([u8; 16]);

impl Ipv6Addr {
    /// Creates a new IPv6 address.
    #[inline]
    pub const fn new(bytes: [u8; 16]) -> Self {
        Ipv6Addr(bytes)
    }

    /// Gets the bytes of the IPv6 address.
    #[inline]
    pub const fn ipv6_bytes(&self) -> [u8; 16] {
        self.0
    }

    /// Converts this `Ipv6Addr` to the IPv6 Solicited-Node Address, used in
    /// Neighbor Discovery. Defined in [RFC 4291 Section 2.7.1].
    ///
    /// [RFC 4291 Section 2.7.1]: https://tools.ietf.org/html/rfc4291#section-2.7.1
    #[inline]
    pub const fn to_solicited_node_address(&self) -> MulticastAddr<Self> {
        // TODO(brunodalbo) benchmark this generation and evaluate if using
        //  bit operations with u128 could be faster. This is very likely
        //  going to be on a hot path.

        // We know we are not breaking the guarantee that `MulticastAddr` provides
        // when calling `new_unchecked` because the address we provide it is
        // a multicast address as defined by RFC 4291 section 2.7.1.
        unsafe {
            MulticastAddr::new_unchecked(Self::new([
                0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xff, self.0[13], self.0[14],
                self.0[15],
            ]))
        }
    }

    /// Checks whether `self` is a valid unicast address.
    ///
    /// A valid unicast address is any unicast address that can be bound to an
    /// interface (not the unspecified or loopback addresses).
    #[inline]
    pub fn is_valid_unicast(&self) -> bool {
        !(self.is_loopback() || !self.is_specified() || self.is_multicast())
    }
}

impl sealed::Sealed for Ipv6Addr {}

impl IpAddress for Ipv6Addr {
    const BYTES: u8 = 16;

    type Version = Ipv6;

    #[inline]
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

    #[inline]
    fn bytes(&self) -> &[u8] {
        &self.0
    }

    #[inline]
    fn into_ip_addr(self) -> IpAddr {
        IpAddr::V6(self)
    }

    #[inline]
    fn is_unicast_in_subnet(&self, _subnet: &Subnet<Self>) -> bool {
        !self.is_multicast() && self.is_specified()
    }

    #[inline]
    fn with<O, F4: Fn(&Ipv4Addr) -> O, F6: Fn(&Ipv6Addr) -> O>(&self, _v4: F4, v6: F6) -> O {
        v6(self)
    }

    #[inline]
    fn with_v4<O, F: Fn(&Ipv4Addr) -> O>(&self, _f: F, default: O) -> O {
        default
    }

    #[inline]
    fn with_v6<O, F: Fn(&Ipv6Addr) -> O>(&self, f: F, _default: O) -> O {
        f(self)
    }

    fn into_subnet_either(subnet: Subnet<Ipv6Addr>) -> SubnetEither {
        SubnetEither::V6(subnet)
    }
}

impl UnicastAddress for Ipv6Addr {
    #[inline]
    fn is_unicast(&self) -> bool {
        !self.is_multicast() && self.is_specified()
    }
}

impl From<[u8; 16]> for Ipv6Addr {
    #[inline]
    fn from(bytes: [u8; 16]) -> Ipv6Addr {
        Ipv6Addr(bytes)
    }
}

#[cfg(std)]
impl From<net::Ipv6Addr> for Ipv6Addr {
    #[inline]
    fn from(ip: net::Ipv6Addr) -> Ipv6Addr {
        Ipv6Addr::new(ip.octets())
    }
}

#[cfg(std)]
impl From<Ipv6Addr> for net::Ipv6Addr {
    #[inline]
    fn from(ip: Ipv6Addr) -> net::Ipv6Addr {
        net::Ipv6Addr::from(ip.0)
    }
}

impl Display for Ipv6Addr {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), fmt::Error> {
        // TODO(joshlf): Implement canonicalization even when the `std` feature
        // is not enabled.

        let to_u16 = |idx| NetworkEndian::read_u16(&self.0[idx..idx + 2]);
        #[cfg(std)]
        return Display::fmt(
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
        );

        #[cfg(not(std))]
        return write!(
            f,
            "{:x}:{:x}:{:x}:{:x}:{:x}:{:x}:{:x}:{:x}",
            to_u16(0),
            to_u16(2),
            to_u16(4),
            to_u16(6),
            to_u16(8),
            to_u16(10),
            to_u16(12),
            to_u16(14),
        );
    }
}

impl Debug for Ipv6Addr {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), fmt::Error> {
        Display::fmt(self, f)
    }
}

/// An IP subnet.
///
/// `Subnet` is a combination of an IP network address and a prefix length.
#[derive(Copy, Clone, Eq, PartialEq, Hash)]
pub struct Subnet<A> {
    // invariant: only contains prefix bits
    network: A,
    prefix: u8,
}

// TODO(joshlf): Currently, we need a separate new_unchecked because trait
// bounds other than Sized are not supported in const fns. Once that
// restriction is lifted, we can make new a const fn.

impl<A> Subnet<A> {
    /// Creates a new subnet without enforcing correctness.
    ///
    /// Unlike `new`, `new_unchecked` does not validate that `prefix` is in the
    /// proper range, and does not check that `network` has only the top
    /// `prefix` bits set. It is up to the caller to guarantee that `prefix` is
    /// in the proper range, and that none of the bits of `network` beyond the
    /// prefix are set.
    #[inline]
    pub const unsafe fn new_unchecked(network: A, prefix: u8) -> Subnet<A> {
        Subnet { network, prefix }
    }
}

impl<A: IpAddress> Subnet<A> {
    /// Creates a new subnet.
    ///
    /// `new` creates a new subnet with the given network address and prefix
    /// length. It returns `None` if `prefix` is longer than the number of bits
    /// in this type of IP address (32 for IPv4 and 128 for IPv6) or if any of
    /// the lower bits (beyond the first `prefix` bits) are set in `network`.
    #[inline]
    pub fn new(network: A, prefix: u8) -> Option<Subnet<A>> {
        // TODO(joshlf): Is there a more efficient way we can perform this
        // check?
        if prefix > A::BYTES * 8 || network != network.mask(prefix) {
            return None;
        }
        Some(Subnet { network, prefix })
    }

    /// Gets the network address component of this subnet.
    ///
    /// `network` returns the network address component of this subnet. Any bits
    /// beyond the prefix will be zero.
    #[inline]
    pub fn network(&self) -> A {
        self.network
    }

    /// Gets the prefix length component of this subnet.
    #[inline]
    pub fn prefix(&self) -> u8 {
        self.prefix
    }

    /// Tests whether an address is in this subnet.
    ///
    /// Tests whether `address` is in this subnet by testing whether the prefix
    /// bits match the prefix bits of the subnet's network address. This is
    /// equivalent to `subnet.network() == address.mask(subnet.prefix())`.
    #[inline]
    pub fn contains(&self, address: &A) -> bool {
        self.network == address.mask(self.prefix)
    }
}

impl Subnet<Ipv4Addr> {
    // TODO(joshlf): Introduce a `BroadcastAddr` witness type, and have
    // `broadcast` return `BroadcastAddr<Ipv4Addr>`.

    /// Gets the broadcast address in this IPv4 subnet.
    #[inline]
    pub fn broadcast(self) -> Ipv4Addr {
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

impl<A: IpAddress> Display for Subnet<A> {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), fmt::Error> {
        write!(f, "{}/{}", self.network, self.prefix)
    }
}

impl<A: IpAddress> Debug for Subnet<A> {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), fmt::Error> {
        write!(f, "{}/{}", self.network, self.prefix)
    }
}

/// An IPv4 subnet or an IPv6 subnet.
///
/// `SubnetEither` is an enum of [`Subnet<Ipv4Addr>`] and `Subnet<Ipv6Addr>`.
///
/// [`Subnet<Ipv4Addr>`]: crate::ip::Subnet
#[allow(missing_docs)]
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub enum SubnetEither {
    V4(Subnet<Ipv4Addr>),
    V6(Subnet<Ipv6Addr>),
}

impl SubnetEither {
    /// Creates a new subnet.
    ///
    /// `new` creates a new subnet with the given network address and prefix
    /// length. It returns `None` if `prefix` is longer than the number of bits
    /// in this type of IP address (32 for IPv4 and 128 for IPv6) or if any of
    /// the lower bits (beyond the first `prefix` bits) are set in `network`.
    #[inline]
    pub fn new(network: IpAddr, prefix: u8) -> Option<SubnetEither> {
        Some(match network {
            IpAddr::V4(network) => SubnetEither::V4(Subnet::new(network, prefix)?),
            IpAddr::V6(network) => SubnetEither::V6(Subnet::new(network, prefix)?),
        })
    }

    /// Gets the network and prefix for this `SubnetEither`.
    #[inline]
    pub fn into_net_prefix(self) -> (IpAddr, u8) {
        match self {
            SubnetEither::V4(v4) => (v4.network.into(), v4.prefix),
            SubnetEither::V6(v6) => (v6.network.into(), v6.prefix),
        }
    }
}

impl<A: IpAddress> From<Subnet<A>> for SubnetEither {
    fn from(subnet: Subnet<A>) -> SubnetEither {
        A::into_subnet_either(subnet)
    }
}

// TODO(joshlf): Is the unicast restriction always necessary, or will some users
// want the AddrSubnet functionality without that restriction?

/// An address and that address' subnet.
///
/// An `AddrSubnet` is a pair of an address and a subnet which maintains the
/// invariant that the address is guaranteed to be a unicast address in the
/// subnet. `S` is the type of address ([`Ipv4Addr`] or [`Ipv6Addr`]), and `A`
/// is the type of the address in the subnet, which is always a witness wrapper
/// around `S`. By default, it is `SpecifiedAddr<S>`.
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub struct AddrSubnet<S: IpAddress, A: Witness<S> = SpecifiedAddr<S>> {
    // TODO(joshlf): Would it be more performant to store these as just an
    // address and subnet mask? It would make the object smaller and so cheaper
    // to pass around, but it would make certain operations more expensive.
    addr: A,
    subnet: Subnet<S>,
}

impl<S: IpAddress, A: Witness<S>> AddrSubnet<S, A> {
    /// Creates a new `AddrSubnet`.
    ///
    /// `new` creates a new `AddrSubnet` with the given address and prefix
    /// length. The network address of the subnet is taken to be the first
    /// `prefix` bits of the address. It returns `None` if `prefix` is longer
    /// than the number of bits in this type of IP address (32 for IPv4 and 128
    /// for IPv6), if `addr` is not a unicast address in the resulting subnet
    /// (see [`IpAddress::is_unicast_in_subnet`]), or if `addr` does not satisfy
    /// the requirements of the witness type `A`.
    #[inline]
    pub fn new(addr: S, prefix: u8) -> Option<AddrSubnet<S, A>> {
        if prefix > S::BYTES * 8 {
            return None;
        }
        let subnet = Subnet { network: addr.mask(prefix), prefix };
        if !addr.is_unicast_in_subnet(&subnet) {
            return None;
        }
        let addr = A::new(addr)?;
        Some(AddrSubnet { addr, subnet })
    }

    /// Gets the subnet.
    #[inline]
    pub fn subnet(&self) -> Subnet<S> {
        self.subnet
    }

    /// Consumes the `AddrSubnet` and returns the address.
    #[inline]
    pub fn into_addr(self) -> A {
        self.addr
    }

    /// Consumes the `AddrSubnet` and returns the subnet.
    #[inline]
    pub fn into_subnet(self) -> Subnet<S> {
        self.subnet
    }

    /// Consumes the `AddrSubnet` and returns the address and subnet
    /// individually.
    #[inline]
    pub fn into_addr_subnet(self) -> (A, Subnet<S>) {
        (self.addr, self.subnet)
    }
}

impl<S: IpAddress, A: Witness<S> + Copy> AddrSubnet<S, A> {
    /// Gets the address.
    #[inline]
    pub fn addr(&self) -> A {
        self.addr
    }
}

/// A type which is a witness to some property about an `IpAddress`.
///
/// `IpAddrWitness` extends [`Witness`] by adding associated types for the IPv4-
/// and IPv6-specific versions of the same witness type. For example, the
/// following implementation is provided for `SpecifiedAddr<IpAddr>`:
///
/// ```rust,ignore
/// impl IpAddrWitness for SpecifiedAddr<IpAddr> {
///     type V4 = SpecifiedAddr<Ipv4Addr>;
///     type V6 = SpecifiedAddr<Ipv6Addr>;
/// }
/// ```
pub trait IpAddrWitness: Witness<IpAddr> {
    type V4: Witness<Ipv4Addr> + Into<Self>;
    type V6: Witness<Ipv6Addr> + Into<Self>;
}

macro_rules! impl_ip_addr_witness {
    ($witness:ident) => {
        impl IpAddrWitness for $witness<IpAddr> {
            type V4 = $witness<Ipv4Addr>;
            type V6 = $witness<Ipv6Addr>;
        }
    };
}

impl_ip_addr_witness!(SpecifiedAddr);
impl_ip_addr_witness!(MulticastAddr);
impl_ip_addr_witness!(LinkLocalAddr);

/// An address and that address' subnet, either IPv4 or IPv6.
///
/// `AddrSubnetEither` is an enum of [`AddrSubnet<Ipv4Addr>`] and
/// `AddrSubnet<Ipv6Addr>`.
///
/// [`AddrSubnet<Ipv4Addr>`]: crate::ip::AddrSubnet
#[allow(missing_docs)]
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub enum AddrSubnetEither<A: IpAddrWitness = SpecifiedAddr<IpAddr>> {
    V4(AddrSubnet<Ipv4Addr, A::V4>),
    V6(AddrSubnet<Ipv6Addr, A::V6>),
}

impl<A: IpAddrWitness> AddrSubnetEither<A> {
    /// Creates a new `AddrSubnetEither`.
    ///
    /// `new` creates a new `AddrSubnetEither` with the given address and prefix
    /// length. It returns `None` under the same conditions as
    /// [`AddrSubnet::new`].
    #[inline]
    pub fn new(addr: IpAddr, prefix: u8) -> Option<AddrSubnetEither<A>> {
        Some(match addr {
            IpAddr::V4(addr) => AddrSubnetEither::V4(AddrSubnet::new(addr, prefix)?),
            IpAddr::V6(addr) => AddrSubnetEither::V6(AddrSubnet::new(addr, prefix)?),
        })
    }

    /// Gets the contained IP address and prefix in this `AddrSubnetEither`.
    #[inline]
    pub fn into_addr_prefix(self) -> (A, u8) {
        match self {
            AddrSubnetEither::V4(v4) => (v4.addr.into(), v4.subnet.prefix),
            AddrSubnetEither::V6(v6) => (v6.addr.into(), v6.subnet.prefix),
        }
    }

    /// Gets the IP address and subnet in this `AddrSubnetEither`.
    #[inline]
    pub fn into_addr_subnet(self) -> (A, SubnetEither) {
        match self {
            AddrSubnetEither::V4(v4) => (v4.addr.into(), SubnetEither::V4(v4.subnet)),
            AddrSubnetEither::V6(v6) => (v6.addr.into(), SubnetEither::V6(v6.subnet)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_loopback_unicast() {
        // The loopback addresses are constructed as `SpecifiedAddr`s directly,
        // bypassing the actual check against `is_specified`. Test that that's
        // actually valid.
        assert!(Ipv4::LOOPBACK_ADDRESS.0.is_specified());
        assert!(Ipv6::LOOPBACK_ADDRESS.0.is_specified());
    }

    #[test]
    fn test_specified() {
        // For types that implement SpecifiedAddress,
        // UnicastAddress::is_unicast, MulticastAddress::is_multicast, and
        // LinkLocalAddress::is_linklocal all imply
        // SpecifiedAddress::is_specified. Test that that's true for both IPv4
        // and IPv6.

        assert!(!Ipv6::UNSPECIFIED_ADDRESS.is_specified());
        assert!(!Ipv4::UNSPECIFIED_ADDRESS.is_specified());

        // Unicast

        assert!(!Ipv6::UNSPECIFIED_ADDRESS.is_unicast());

        let unicast = Ipv6Addr([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);
        assert!(unicast.is_unicast());
        assert!(unicast.is_specified());

        // Multicast

        assert!(!Ipv4::UNSPECIFIED_ADDRESS.is_multicast());
        assert!(!Ipv6::UNSPECIFIED_ADDRESS.is_multicast());

        let multicast = Ipv4::MULTICAST_SUBNET.network;
        assert!(multicast.is_multicast());
        assert!(multicast.is_specified());
        let multicast = Ipv6::MULTICAST_SUBNET.network;
        assert!(multicast.is_multicast());
        assert!(multicast.is_specified());

        // Link-local

        assert!(!Ipv4::UNSPECIFIED_ADDRESS.is_linklocal());
        assert!(!Ipv6::UNSPECIFIED_ADDRESS.is_linklocal());

        let link_local = Ipv4::LINK_LOCAL_UNICAST_SUBNET.network;
        assert!(link_local.is_linklocal());
        assert!(link_local.is_specified());
        let link_local = Ipv4::LINK_LOCAL_MULTICAST_SUBNET.network;
        assert!(link_local.is_linklocal());
        assert!(link_local.is_specified());
        let link_local = Ipv6::LINK_LOCAL_UNICAST_SUBNET.network;
        assert!(link_local.is_linklocal());
        assert!(link_local.is_specified());
        let mut link_local = Ipv6::MULTICAST_SUBNET.network;
        link_local.0[1] = 0x02;
        assert!(link_local.is_linklocal());
        assert!(link_local.is_specified());
    }

    #[test]
    fn test_linklocal() {
        // IPv4
        assert!(Ipv4::LINK_LOCAL_UNICAST_SUBNET.network.is_linklocal());
        assert!(Ipv4::LINK_LOCAL_MULTICAST_SUBNET.network.is_linklocal());

        // IPv6
        assert!(Ipv6::LINK_LOCAL_UNICAST_SUBNET.network.is_linklocal());
        let mut addr = Ipv6::MULTICAST_SUBNET.network;
        for flags in 0..=0x0F {
            // Set the scope to link-local and the flags to `flags`.
            addr.0[1] = (flags << 4) | 0x02;
            // Test that a link-local multicast address is always considered
            // link-local regardless of which flags are set.
            assert!(addr.is_linklocal());
        }

        // Test that a non-multicast address (outside of the link-local subnet)
        // is never considered link-local even if the bits are set that, in a
        // multicast address, would put it in the link-local scope.
        let mut addr = Ipv6::LOOPBACK_ADDRESS.get();
        // Explicitly set the scope to link-local.
        addr.0[1] = 0x02;
        assert!(!addr.is_linklocal());
    }

    #[test]
    fn test_subnet_new() {
        Subnet::new(Ipv4Addr::new([255, 255, 255, 255]), 32).unwrap();
        // Prefix exceeds 32 bits
        assert_eq!(Subnet::new(Ipv4Addr::new([255, 255, 0, 0]), 33), None);
        // Network address has more than top 8 bits set
        assert_eq!(Subnet::new(Ipv4Addr::new([255, 255, 0, 0]), 8), None);

        AddrSubnet::<_, SpecifiedAddr<_>>::new(Ipv4Addr::new([1, 2, 3, 4]), 32).unwrap();
        // The unspecified address is not considered to be a unicast address in
        // any subnet (use assert, not assert_eq, because AddrSubnet doesn't
        // impl Debug)
        assert!(AddrSubnet::<_, SpecifiedAddr<_>>::new(Ipv4::UNSPECIFIED_ADDRESS, 16) == None);
        assert!(AddrSubnet::<_, SpecifiedAddr<_>>::new(Ipv6::UNSPECIFIED_ADDRESS, 64) == None);
        // Prefix exceeds 32/128 bits
        assert!(AddrSubnet::<_, SpecifiedAddr<_>>::new(Ipv4Addr::new([1, 2, 3, 4]), 33) == None);
        assert!(
            AddrSubnet::<_, SpecifiedAddr<_>>::new(
                Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]),
                129
            ) == None
        );
        // Global broadcast
        assert!(
            AddrSubnet::<_, SpecifiedAddr<_>>::new(Ipv4::GLOBAL_BROADCAST_ADDRESS.into_addr(), 16)
                == None
        );
        // Subnet broadcast
        assert!(
            AddrSubnet::<_, SpecifiedAddr<_>>::new(Ipv4Addr::new([192, 168, 255, 255]), 16) == None
        );
        // Multicast
        assert!(AddrSubnet::<_, SpecifiedAddr<_>>::new(Ipv4Addr::new([224, 0, 0, 1]), 16) == None);
        assert!(
            AddrSubnet::<_, SpecifiedAddr<_>>::new(
                Ipv6Addr::new([0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]),
                64
            ) == None
        );

        // If we use the `LinkLocalAddr` witness type, then non link-local
        // addresses are rejected. Note that this address was accepted above
        // when `SpecifiedAddr` was used.
        assert!(AddrSubnet::<_, LinkLocalAddr<_>>::new(Ipv4Addr::new([1, 2, 3, 4]), 32) == None);
    }

    #[test]
    fn test_is_unicast_in_subnet() {
        // Valid unicast in subnet
        assert!(Ipv4Addr::new([1, 2, 3, 4])
            .is_unicast_in_subnet(&Subnet::new(Ipv4Addr::new([1, 2, 0, 0]), 16).unwrap()));
        assert!(Ipv6Addr::new([1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1])
            .is_unicast_in_subnet(
                &Subnet::new(Ipv6Addr::new([1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]), 64)
                    .unwrap()
            ));

        // Unspecified address
        assert!(!Ipv4::UNSPECIFIED_ADDRESS
            .is_unicast_in_subnet(&Subnet::new(Ipv4::UNSPECIFIED_ADDRESS, 16).unwrap()));
        assert!(!Ipv6::UNSPECIFIED_ADDRESS
            .is_unicast_in_subnet(&Subnet::new(Ipv6::UNSPECIFIED_ADDRESS, 64).unwrap()));
        // The "31- or 32-bit prefix" exception doesn't apply to the unspecified
        // address (IPv4 only).
        assert!(!Ipv4::UNSPECIFIED_ADDRESS
            .is_unicast_in_subnet(&Subnet::new(Ipv4::UNSPECIFIED_ADDRESS, 31).unwrap()));
        assert!(!Ipv4::UNSPECIFIED_ADDRESS
            .is_unicast_in_subnet(&Subnet::new(Ipv4::UNSPECIFIED_ADDRESS, 32).unwrap()));
        // All-zeroes host part (IPv4 only)
        assert!(!Ipv4Addr::new([1, 2, 0, 0])
            .is_unicast_in_subnet(&Subnet::new(Ipv4Addr::new([1, 2, 0, 0]), 16).unwrap()));
        // Exception: 31- or 32-bit prefix (IPv4 only)
        assert!(Ipv4Addr::new([1, 2, 3, 0])
            .is_unicast_in_subnet(&Subnet::new(Ipv4Addr::new([1, 2, 3, 0]), 31).unwrap()));
        assert!(Ipv4Addr::new([1, 2, 3, 0])
            .is_unicast_in_subnet(&Subnet::new(Ipv4Addr::new([1, 2, 3, 0]), 32).unwrap()));
        // Global broadcast (IPv4 only)
        assert!(!Ipv4::GLOBAL_BROADCAST_ADDRESS
            .into_addr()
            .is_unicast_in_subnet(&Subnet::new(Ipv4Addr::new([128, 0, 0, 0]), 1).unwrap()));
        // Subnet broadcast (IPv4 only)
        assert!(!Ipv4Addr::new([1, 2, 255, 255])
            .is_unicast_in_subnet(&Subnet::new(Ipv4Addr::new([1, 2, 0, 0]), 16).unwrap()));
        // Exception: 31- or 32-bit prefix (IPv4 only)
        assert!(Ipv4Addr::new([1, 2, 255, 255])
            .is_unicast_in_subnet(&Subnet::new(Ipv4Addr::new([1, 2, 255, 254]), 31).unwrap()));
        assert!(Ipv4Addr::new([1, 2, 255, 255])
            .is_unicast_in_subnet(&Subnet::new(Ipv4Addr::new([1, 2, 255, 255]), 32).unwrap()));
        // Multicast
        assert!(!Ipv4Addr::new([224, 0, 0, 1]).is_unicast_in_subnet(&Ipv4::MULTICAST_SUBNET));
        assert!(!Ipv6Addr::new([0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1])
            .is_unicast_in_subnet(&Ipv6::MULTICAST_SUBNET));
        // Class E (IPv4 only)
        assert!(!Ipv4Addr::new([240, 0, 0, 1]).is_unicast_in_subnet(&Ipv4::CLASS_E_SUBNET));
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
        let solicited =
            Ipv6Addr::new([0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xff, 0xb5, 0x5a, 0xa0]);
        assert_eq!(addr.to_solicited_node_address().get(), solicited);
    }

    #[test]
    fn test_ipv6_address_types() {
        assert!(!Ipv6Addr::new([0; 16]).is_specified());
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

        let multi =
            Ipv6Addr::new([0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xff, 0xb5, 0x5a, 0xa0]);
        assert!(multi.is_multicast());
        assert!(!multi.is_valid_unicast());
    }
}
