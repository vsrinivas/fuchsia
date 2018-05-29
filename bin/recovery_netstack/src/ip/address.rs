// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::{Debug, Display};

use byteorder::{BigEndian, ByteOrder};

/// An IP protocol version.
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub enum IpVersion {
    V4,
    V6,
}

impl IpVersion {
    pub fn version_number(&self) -> u8 {
        match self {
            IpVersion::V4 => 4,
            IpVersion::V6 => 6,
        }
    }
}

mod sealed {
    // Ensure that only Ipv4 and Ipv6 can implement IpVersion and that only
    // Ipv4Addr and Ipv6Addr can implement IpAddr.
    pub trait Sealed {}
}

/// An trait for IP protocol versions.
///
/// `Ip` encapsulates the details of a version of the IP protocol. It includes
/// the `IpVersion` enum (`VERSION`) and address type (`Addr`). It is
/// implemented by `Ipv4` and `Ipv6`.
pub trait Ip: self::sealed::Sealed {
    /// The IP version.
    ///
    /// `V4` for IPv4 and `V6` for IPv6.
    const VERSION: IpVersion;

    /// The default loopback address.
    ///
    /// When sending packets to a loopback interface, this address is used as
    /// the source address. It is an address in the loopback subnet.
    const LOOPBACK_ADDRESS: Self::Addr;

    /// The subnet of loopback addresses.
    ///
    /// Addresses in this subnet must not appear outside a host, and may only be
    /// used for loopback interfaces.
    const LOOPBACK_SUBNET: Subnet<Self::Addr>;

    /// The address type for this IP version.
    ///
    /// `Ipv4Addr` for IPv4 and `Ipv6Addr` for IPv6.
    type Addr: IpAddr;
}

/// IPv4.
///
/// `Ipv4` implements `Ip` for IPv4.
#[derive(Debug)]
pub struct Ipv4;

impl Ip for Ipv4 {
    const VERSION: IpVersion = IpVersion::V4;
    // https://tools.ietf.org/html/rfc5735#section-3
    const LOOPBACK_ADDRESS: Ipv4Addr = Ipv4Addr::new([127, 0, 0, 1]);
    const LOOPBACK_SUBNET: Subnet<Ipv4Addr> = Subnet {
        network: Ipv4Addr::new([127, 0, 0, 0]),
        prefix: 8,
    };
    type Addr = Ipv4Addr;
}

impl self::sealed::Sealed for Ipv4 {}

/// IPv6.
///
/// `Ipv6` implements `Ip` for IPv6.
#[derive(Debug)]
pub struct Ipv6;

impl Ip for Ipv6 {
    const VERSION: IpVersion = IpVersion::V6;
    const LOOPBACK_ADDRESS: Ipv6Addr = Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);
    const LOOPBACK_SUBNET: Subnet<Ipv6Addr> = Subnet {
        network: Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]),
        prefix: 128,
    };
    type Addr = Ipv6Addr;
}

impl self::sealed::Sealed for Ipv6 {}

/// An IPv4 or IPv6 address.
pub trait IpAddr
where
    Self: Eq + Copy + Display + self::sealed::Sealed,
{
    /// The number of bytes in an address of this type.
    ///
    /// 4 for IPv4 and 16 for IPv6.
    const BYTES: u8;

    /// The IP version type of this address.
    ///
    /// `Ipv4` for `Ipv4Addr` and `Ipv6` for `Ipv6Addr`.
    type Version: Ip;

    /// Get the underlying bytes of the address.
    fn bytes(&self) -> &[u8];

    /// Mask off the top bits of the address.
    ///
    /// Return a copy of `self` where all but the top `bits` bits are set to 0.
    fn mask(&self, bits: u8) -> Self;
}

/// An IPv4 address.
#[derive(Copy, Clone, Default, PartialEq, Eq)]
pub struct Ipv4Addr([u8; 4]);

impl Ipv4Addr {
    /// Create a new IPv4 address.
    pub const fn new(bytes: [u8; 4]) -> Self {
        Ipv4Addr(bytes)
    }

    pub const fn ipv4_bytes(&self) -> [u8; 4] {
        self.0
    }
}

impl IpAddr for Ipv4Addr {
    const BYTES: u8 = 4;

    type Version = Ipv4;

    fn mask(&self, bits: u8) -> Self {
        assert!(bits <= 32);
        if bits == 32 {
            // shifting left by the size of the value is undefined
            Ipv4Addr([0; 4])
        } else {
            let mask = <u32>::max_value() << (32 - bits);
            let masked = BigEndian::read_u32(&self.0) & mask;
            let mut ret = Ipv4Addr::default();
            BigEndian::write_u32(&mut ret.0, masked);
            ret
        }
    }

    fn bytes(&self) -> &[u8] {
        &self.0
    }
}

impl self::sealed::Sealed for Ipv4Addr {}

impl Display for Ipv4Addr {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> Result<(), ::std::fmt::Error> {
        write!(f, "{}.{}.{}.{}", self.0[0], self.0[1], self.0[2], self.0[3])
    }
}

impl Debug for Ipv4Addr {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> Result<(), ::std::fmt::Error> {
        Display::fmt(self, f)
    }
}

/// An IPv6 address.
#[derive(Copy, Clone, Default, PartialEq, Eq)]
pub struct Ipv6Addr([u8; 16]);

impl Ipv6Addr {
    /// Create a new IPv6 address.
    pub const fn new(bytes: [u8; 16]) -> Self {
        Ipv6Addr(bytes)
    }

    pub const fn ipv6_bytes(&self) -> [u8; 16] {
        self.0
    }
}

impl IpAddr for Ipv6Addr {
    const BYTES: u8 = 16;

    type Version = Ipv6;

    fn mask(&self, bits: u8) -> Self {
        assert!(bits <= 128);
        if bits == 128 {
            // shifting left by the size of the value is undefined
            Ipv6Addr([0; 16])
        } else {
            let mask = <u128>::max_value() << (128 - bits);
            let masked = BigEndian::read_u128(&self.0) & mask;
            let mut ret = Ipv6Addr::default();
            BigEndian::write_u128(&mut ret.0, masked);
            ret
        }
    }

    fn bytes(&self) -> &[u8] {
        &self.0
    }
}

impl self::sealed::Sealed for Ipv6Addr {}

impl Display for Ipv6Addr {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> Result<(), ::std::fmt::Error> {
        // TODO(joshlf): Replace longest run of zeros with ::.
        let to_u16 = |idx| BigEndian::read_u16(&self.0[idx..idx + 2]);
        write!(
            f,
            "{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}:{:04x}",
            to_u16(0),
            to_u16(2),
            to_u16(4),
            to_u16(6),
            to_u16(8),
            to_u16(10),
            to_u16(12),
            to_u16(14)
        )
    }
}

impl Debug for Ipv6Addr {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> Result<(), ::std::fmt::Error> {
        Display::fmt(self, f)
    }
}

/// An IP subnet.
///
/// `Subnet` is a combination of an IP network address and a prefix length.
#[derive(Copy, Clone)]
pub struct Subnet<A: IpAddr> {
    // invariant: normalized to contain only prefix bits
    network: A,
    prefix: u8,
}

impl<A: IpAddr> Subnet<A> {
    /// Create a new subnet.
    ///
    /// Create a new subnet with the given network address and prefix length.
    ///
    /// # Panics
    ///
    /// `new` panics if `prefix` is longer than the number of bits in this type
    /// of IP address (32 for IPv4 and 128 for IPv6).
    pub fn new(network: A, prefix: u8) -> Subnet<A> {
        assert!(prefix <= A::BYTES * 8);
        let network = network.mask(prefix);
        Subnet { network, prefix }
    }

    /// Get the network address component of this subnet.
    ///
    /// `network` returns the network address component of this subnet. Any bits
    /// beyond the prefix will be zero.
    pub fn network(&self) -> A {
        self.network
    }

    /// Get the prefix length component of this subnet.
    pub fn prefix(&self) -> u8 {
        self.prefix
    }

    /// Test whether an address is in this subnet.
    ///
    /// Test whether `address` is in this subnet by testing whether the prefix
    /// bits match the prefix bits of the subnet's network address. This is
    /// equivalent to `subnet.network() == address.mask(subnet.prefix())`.
    pub fn contains(&self, address: A) -> bool {
        self.network == address.mask(self.prefix)
    }
}

impl<A: IpAddr> Display for Subnet<A> {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> Result<(), ::std::fmt::Error> {
        write!(f, "{}/{}", self.network, self.prefix)
    }
}

impl<A: IpAddr> Debug for Subnet<A> {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> Result<(), ::std::fmt::Error> {
        write!(f, "{}/{}", self.network, self.prefix)
    }
}