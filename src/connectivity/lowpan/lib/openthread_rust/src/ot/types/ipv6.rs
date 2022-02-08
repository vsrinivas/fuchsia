// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

use core::fmt::{Debug, Formatter};
use std::marker::PhantomData;
use std::net::SocketAddrV6;

/// IPv6 Address Type. Functional equivalent of [`otsys::otIp6Address`](crate::otsys::otIp6Address).
///
/// ## NOTES ON SAFETY ##
///
/// Here we are making the assumption that a [`std::net::Ipv6Addr`] can
/// be freely and safely transmuted to and from an [`otIp6Address`](crate::otsys::otIp6Address).
/// Doing this is very convenient. On the face of it, this might seem like a safe assumption:
/// IPv6 addresses are always 16 bytes long, that ought to be fine, right? And, in my testing,
/// it does seem to work.
///
/// But thinking more carefully about it it seems that this is not *guaranteed* to
/// be the case. This is supported by the fact that the rust API appears to avoid what
/// would otherwise be obvious additions, such as a method to return the IPv6 address as
/// a slice (`octets()` returns a fixed-size array).
///
/// As a crutch, I've added static assertions in various places to verify my assumptions.
/// This effectively eliminates chances of undefined behavior causing problems, but does so
/// at the expense of failing to compile when those assumptions are not met... So we may
/// want to revisit this approach in the future if this is a big concern.
pub type Ip6Address = std::net::Ipv6Addr;

impl Transparent for std::net::Ipv6Addr {
    fn from_ot(x: otIp6Address) -> std::net::Ipv6Addr {
        unsafe { x.mFields.m8.into() }
    }

    fn into_ot(self) -> otIp6Address {
        otIp6Address { mFields: otIp6Address__bindgen_ty_1 { m8: self.octets() } }
    }
}

unsafe impl OtCastable for std::net::Ipv6Addr {
    type OtType = otIp6Address;

    fn as_ot_ptr(&self) -> *const otIp6Address {
        sa::assert_eq_size!(Ip6Address, otIp6Address);
        sa::assert_eq_align!(Ip6Address, otIp6Address);
        self as *const Self as *const otIp6Address
    }

    fn as_ot_mut_ptr(&mut self) -> *mut Self::OtType {
        self as *mut Self as *mut Self::OtType
    }

    unsafe fn ref_from_ot_ptr<'a>(ptr: *const otIp6Address) -> Option<&'a Self> {
        if ptr.is_null() {
            None
        } else {
            sa::assert_eq_size!(Ip6Address, otIp6Address);
            sa::assert_eq_align!(Ip6Address, otIp6Address);

            // SAFETY: The safety of this dereference is ensured by the above two static assertions.
            Some(&*(ptr as *const Self))
        }
    }

    unsafe fn mut_from_ot_mut_ptr<'a>(ptr: *mut otIp6Address) -> Option<&'a mut Self> {
        if ptr.is_null() {
            None
        } else {
            sa::assert_eq_size!(Ip6Address, otIp6Address);
            sa::assert_eq_align!(Ip6Address, otIp6Address);

            // SAFETY: The safety of this dereference is ensured by the above two static assertions.
            Some(&mut *(ptr as *mut Self))
        }
    }
}

/// Data type representing a 64-bit IPv6 network prefix.
///
/// Functional equivalent of [`otsys::otIp6NetworkPrefix`](crate::otsys::otIp6NetworkPrefix).
#[derive(Default, Clone, Copy)]
#[repr(transparent)]
pub struct Ip6NetworkPrefix(pub otIp6NetworkPrefix);

impl_ot_castable!(Ip6NetworkPrefix, otIp6NetworkPrefix);

impl PartialEq for Ip6NetworkPrefix {
    fn eq(&self, other: &Self) -> bool {
        self.0.m8 == other.0.m8
    }
}

impl Eq for Ip6NetworkPrefix {}

impl Debug for Ip6NetworkPrefix {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        std::net::Ipv6Addr::from(*self).fmt(f)?;
        write!(f, "/64")
    }
}

impl std::fmt::Display for Ip6NetworkPrefix {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        std::fmt::Debug::fmt(self, f)
    }
}

impl Ip6NetworkPrefix {
    /// Returns this network prefix as a byte slice.
    pub fn as_slice(&self) -> &[u8] {
        &self.0.m8
    }

    /// Returns this network prefix as an 8-byte array by value.
    pub fn octets(&self) -> [u8; 8] {
        let mut octets = [0u8; 8];
        octets.clone_from_slice(self.as_slice());
        octets
    }
}

impl From<[u8; 8]> for Ip6NetworkPrefix {
    fn from(m8: [u8; 8]) -> Self {
        Self(otIp6NetworkPrefix { m8 })
    }
}

impl From<Ip6NetworkPrefix> for std::net::Ipv6Addr {
    fn from(prefix: Ip6NetworkPrefix) -> Self {
        let mut octets = [0u8; 16];
        octets[0..8].clone_from_slice(prefix.as_slice());
        octets.into()
    }
}

impl From<std::net::Ipv6Addr> for Ip6NetworkPrefix {
    fn from(x: std::net::Ipv6Addr) -> Self {
        let mut ret = Ip6NetworkPrefix::default();
        ret.0.m8.clone_from_slice(&x.octets()[0..8]);
        ret
    }
}

/// Data type representing IPv6 address information.
///
/// Functional equivalent of [`otsys::otIp6AddressInfo`](crate::otsys::otIp6AddressInfo).
/// Note that this type has a lifetime because the original OpenThread
/// type includes a pointer to the IPv6 address.
#[derive(Default, Clone, Copy)]
#[repr(transparent)]
pub struct Ip6AddressInfo<'a>(otIp6AddressInfo, PhantomData<*mut &'a ()>);

impl_ot_castable!(lifetime Ip6AddressInfo<'_>, otIp6AddressInfo, Default::default());

impl<'a> Debug for Ip6AddressInfo<'a> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        self.addr().fmt(f)?;
        write!(f, "/{} {:?}", self.prefix_len(), self.scope())?;
        if self.is_preferred() {
            write!(f, " PREFERRED")?;
        }

        Ok(())
    }
}

impl<'a> Ip6AddressInfo<'a> {
    /// Returns a reference to the `Ip6Address`.
    pub fn addr(&self) -> &'a Ip6Address {
        unsafe { Ip6Address::ref_from_ot_ptr(self.0.mAddress).unwrap() }
    }

    /// Returns true if this address is preferred.
    pub fn is_preferred(&self) -> bool {
        self.0.mPreferred()
    }

    /// Returns the prefix length of this address.
    pub fn prefix_len(&self) -> u8 {
        self.0.mPrefixLength
    }

    /// Returns the scope of this address
    pub fn scope(&self) -> Scope {
        Scope(self.0.mScope())
    }

    /// Returns true if this address is a multicast address.
    pub fn is_multicast(&self) -> bool {
        self.addr().is_multicast()
    }
}

/// Type representing an IPv6 scope, as represented on IPv6 multicast addresses.
#[derive(Copy, Clone, Eq, PartialEq, Ord, PartialOrd)]
#[repr(transparent)]
pub struct Scope(pub u8);

#[allow(missing_docs)]
impl Scope {
    pub const INTERFACE_LOCAL: Scope = Scope(0x1);
    pub const LINK_LOCAL: Scope = Scope(0x2);
    pub const REALM_LOCAL: Scope = Scope(0x3);
    pub const ADMIN_LOCAL: Scope = Scope(0x4);
    pub const SITE_LOCAL: Scope = Scope(0x5);
    pub const ORGANIZATION_LOCAL: Scope = Scope(0x8);
    pub const GLOBAL: Scope = Scope(0xe);
}

impl<'a> Debug for Scope {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match *self {
            Self::INTERFACE_LOCAL => write!(f, "Scope::INTERFACE_LOCAL"),
            Self::LINK_LOCAL => write!(f, "Scope::LINK_LOCAL"),
            Self::REALM_LOCAL => write!(f, "Scope::REALM_LOCAL"),
            Self::ADMIN_LOCAL => write!(f, "Scope::ADMIN_LOCAL"),
            Self::SITE_LOCAL => write!(f, "Scope::SITE_LOCAL"),
            Self::ORGANIZATION_LOCAL => write!(f, "Scope::ORGANIZATION_LOCAL"),
            Self::GLOBAL => write!(f, "Scope::GLOBAL"),
            Scope(x) => write!(f, "Scope({})", x),
        }
    }
}

impl From<Scope> for u8 {
    fn from(x: Scope) -> Self {
        x.0
    }
}

impl From<Scope> for u32 {
    fn from(x: Scope) -> Self {
        x.0 as u32
    }
}

/// Type representing the origin of an address
#[derive(Copy, Clone, Eq, PartialEq, Ord, PartialOrd)]
pub struct AddressOrigin(pub u8);

impl AddressOrigin {
    /// Functional equivalent of
    /// [`otsys::OT_ADDRESS_ORIGIN_DHCPV6`](crate::otsys::OT_ADDRESS_ORIGIN_DHCPV6).
    pub const DHCPV6: AddressOrigin = AddressOrigin(OT_ADDRESS_ORIGIN_DHCPV6 as u8);

    /// Functional equivalent of
    /// [`otsys::OT_ADDRESS_ORIGIN_MANUAL`](crate::otsys::OT_ADDRESS_ORIGIN_MANUAL).
    pub const MANUAL: AddressOrigin = AddressOrigin(OT_ADDRESS_ORIGIN_MANUAL as u8);

    /// Functional equivalent of
    /// [`otsys::OT_ADDRESS_ORIGIN_SLAAC`](crate::otsys::OT_ADDRESS_ORIGIN_SLAAC).
    pub const SLAAC: AddressOrigin = AddressOrigin(OT_ADDRESS_ORIGIN_SLAAC as u8);

    /// Functional equivalent of
    /// [`otsys::OT_ADDRESS_ORIGIN_THREAD`](crate::otsys::OT_ADDRESS_ORIGIN_THREAD).
    pub const THREAD: AddressOrigin = AddressOrigin(OT_ADDRESS_ORIGIN_THREAD as u8);
}

impl<'a> Debug for AddressOrigin {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match *self {
            Self::DHCPV6 => write!(f, "AddressOrigin::DHCPV6"),
            Self::MANUAL => write!(f, "AddressOrigin::MANUAL"),
            Self::SLAAC => write!(f, "AddressOrigin::SLAAC"),
            Self::THREAD => write!(f, "AddressOrigin::THREAD"),
            AddressOrigin(x) => write!(f, "AddressOrigin({})", x),
        }
    }
}

/// Data type representing IPv6 address from a network interface.
/// Functional equivalent of [`otsys::otNetifAddress`](crate::otsys::otNetifAddress).
#[derive(Default, Clone, Copy)]
#[repr(transparent)]
pub struct NetifAddress(otNetifAddress);

impl_ot_castable!(NetifAddress, otNetifAddress);

impl Debug for NetifAddress {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        self.addr().fmt(f)?;
        write!(f, "/{} {:?}", self.prefix_len(), self.address_origin())?;
        if let Some(scope) = self.scope() {
            write!(f, " {:?}", scope)?;
        }
        if self.is_valid() {
            write!(f, " VALID")?;
        }
        if self.is_preferred() {
            write!(f, " PREFERRED")?;
        }
        if self.is_rloc() {
            write!(f, " RLOC")?;
        }
        Ok(())
    }
}

impl std::fmt::Display for NetifAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        std::fmt::Debug::fmt(self, f)
    }
}

impl NetifAddress {
    /// Creates a new instance from an Ipv6 address and prefix length.
    ///
    /// The resulting instance will have the `valid` and `preferred` bits set.
    pub fn new(addr: std::net::Ipv6Addr, prefix_len: u8) -> NetifAddress {
        let mut ret = NetifAddress(otNetifAddress {
            mAddress: addr.into_ot(),
            mPrefixLength: prefix_len,
            mAddressOrigin: 0,
            _bitfield_align_1: [],
            _bitfield_1: Default::default(),
            mNext: std::ptr::null_mut() as *mut otNetifAddress,
        });
        ret.set_valid(true);
        ret.set_preferred(true);
        ret
    }

    /// Returns a reference to the IPv6 address.
    pub fn addr(&self) -> &Ip6Address {
        Ip6Address::ref_from_ot_ref(&self.0.mAddress)
    }

    /// Returns the prefix length for this address.
    pub fn prefix_len(&self) -> u8 {
        self.0.mPrefixLength
    }

    /// Returns the address origin for this address.
    pub fn address_origin(&self) -> AddressOrigin {
        AddressOrigin(self.0.mAddressOrigin)
    }

    /// Returns the scope override for this address.
    pub fn scope(&self) -> Option<Scope> {
        if self.0.mScopeOverrideValid() {
            Some(Scope(self.0.mScopeOverride().try_into().expect("NetifAddress: invalid scope")))
        } else {
            None
        }
    }

    /// Sets the scope override for this address.
    pub fn set_scope(&mut self, scope: Option<Scope>) {
        if let Some(scope) = scope {
            self.0.set_mScopeOverrideValid(true);
            self.0.set_mScopeOverride(scope.into());
        } else {
            self.0.set_mScopeOverrideValid(false);
        }
    }

    /// Returns true if this address is preferred.
    pub fn is_preferred(&self) -> bool {
        self.0.mPreferred()
    }

    /// Sets the `preferred` bit on this address.
    pub fn set_preferred(&mut self, x: bool) {
        self.0.set_mPreferred(x)
    }

    /// Returns true if this address is an RLOC.
    pub fn is_rloc(&self) -> bool {
        self.0.mRloc()
    }

    /// Sets the `rloc` bit on this address.
    pub fn set_rloc(&mut self, x: bool) {
        self.0.set_mRloc(x)
    }

    /// Returns true if this address is valid.
    pub fn is_valid(&self) -> bool {
        self.0.mValid()
    }

    /// Sets the `valid` bit on this address.
    pub fn set_valid(&mut self, x: bool) {
        self.0.set_mValid(x)
    }
}

/// IPv6 subnet prefix with an arbitrary prefix length.
/// Functional equivalent of [`otsys::otIp6Prefix`](crate::otsys::otIp6Prefix).
#[derive(Default, Clone, Copy)]
#[repr(transparent)]
pub struct Ip6Prefix(pub otIp6Prefix);

impl_ot_castable!(Ip6Prefix, otIp6Prefix);

impl PartialEq for Ip6Prefix {
    fn eq(&self, other: &Self) -> bool {
        self.addr() == other.addr() && self.prefix_len() == other.prefix_len()
    }
}

impl Eq for Ip6Prefix {}

impl Debug for Ip6Prefix {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        self.addr().fmt(f)?;
        write!(f, "/{}", self.prefix_len())
    }
}

impl std::fmt::Display for Ip6Prefix {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        std::fmt::Debug::fmt(self, f)
    }
}

impl Ip6Prefix {
    /// Creates a new `Ip6Prefix` from an Ipv6 address and prefix length.
    pub fn new<T: Into<Ip6Address>>(addr: T, prefix_len: u8) -> Ip6Prefix {
        Ip6Prefix(otIp6Prefix { mPrefix: addr.into().into_ot(), mLength: prefix_len })
    }

    /// Returns a reference to the IPv6 address.
    pub fn addr(&self) -> &Ip6Address {
        Ip6Address::ref_from_ot_ref(&self.0.mPrefix)
    }

    /// Returns the prefix length.
    pub fn prefix_len(&self) -> u8 {
        self.0.mLength
    }
}

/// Functional equivalent of [`otsys::otSockAddr`](crate::otsys::otSockAddr).
#[derive(Default, Clone, Copy)]
#[repr(transparent)]
pub struct SockAddr(pub otSockAddr);

impl_ot_castable!(SockAddr, otSockAddr);

impl PartialEq for SockAddr {
    fn eq(&self, other: &Self) -> bool {
        self.addr() == other.addr() && self.port() == other.port()
    }
}

impl Eq for SockAddr {}

impl Debug for SockAddr {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "[")?;
        self.addr().fmt(f)?;
        write!(f, "]:{}", self.port())
    }
}

impl std::fmt::Display for SockAddr {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        std::fmt::Debug::fmt(self, f)
    }
}

impl SockAddr {
    /// Creates a new `ot::SockAddr` from an address and port.
    pub fn new(addr: Ip6Address, port: u16) -> Self {
        SockAddr(otSockAddr { mAddress: addr.into_ot(), mPort: port })
    }

    /// Returns the IPv6 address.
    pub fn addr(&self) -> Ip6Address {
        Ip6Address::from_ot(self.0.mAddress)
    }

    /// Returns the port number.
    pub fn port(&self) -> u16 {
        self.0.mPort
    }
}

impl From<(Ip6Address, u16)> for SockAddr {
    fn from(x: (Ip6Address, u16)) -> Self {
        Self::new(x.0, x.1)
    }
}

impl From<Ip6Address> for SockAddr {
    fn from(x: Ip6Address) -> Self {
        Self::new(x, 0)
    }
}

impl From<std::net::SocketAddrV6> for SockAddr {
    fn from(x: std::net::SocketAddrV6) -> Self {
        SockAddr::new(x.ip().clone(), x.port())
    }
}

impl From<SockAddr> for std::net::SocketAddrV6 {
    fn from(x: SockAddr) -> Self {
        SocketAddrV6::new(x.addr(), x.port(), 0, 0)
    }
}

impl From<SockAddr> for std::net::SocketAddr {
    fn from(x: SockAddr) -> Self {
        std::net::SocketAddr::V6(x.into())
    }
}

/// This enumeration defines the OpenThread network interface identifiers.
///
/// Functional equivalent of [`otsys::otNetifIdentifier`](crate::otsys::otNetifIdentifier).
#[derive(Debug, Copy, Clone, Eq, Ord, PartialOrd, PartialEq, num_derive::FromPrimitive)]
#[allow(missing_docs)]
pub enum NetifIdentifier {
    /// Functional equivalent of [`otsys::otNetifIdentifier_OT_NETIF_BACKBONE`](crate::otsys::otNetifIdentifier_OT_NETIF_BACKBONE).
    Backbone = otNetifIdentifier_OT_NETIF_BACKBONE as isize,

    /// Functional equivalent of [`otsys::otNetifIdentifier_OT_NETIF_THREAD`](crate::otsys::otNetifIdentifier_OT_NETIF_THREAD).
    Thread = otNetifIdentifier_OT_NETIF_THREAD as isize,

    /// Functional equivalent of [`otsys::otNetifIdentifier_OT_NETIF_UNSPECIFIED`](crate::otsys::otNetifIdentifier_OT_NETIF_UNSPECIFIED).
    Unspecified = otNetifIdentifier_OT_NETIF_UNSPECIFIED as isize,
}

impl From<otNetifIdentifier> for NetifIdentifier {
    fn from(x: otNetifIdentifier) -> Self {
        use num::FromPrimitive;
        Self::from_u32(x).expect(format!("Unknown otNetifIdentifier value: {}", x).as_str())
    }
}

impl From<NetifIdentifier> for otNetifIdentifier {
    fn from(x: NetifIdentifier) -> Self {
        x as otNetifIdentifier
    }
}
