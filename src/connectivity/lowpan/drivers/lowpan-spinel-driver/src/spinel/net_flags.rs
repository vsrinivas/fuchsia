// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use core::convert::{TryFrom, TryInto};
use fidl_fuchsia_lowpan_device::RoutePreference;
use spinel_pack::*;
use std::io;
use thiserror::Error;

#[derive(Default, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct NetFlags(u8);

#[derive(Error, Debug, Default, Clone, Copy, Eq, PartialEq, Hash)]
pub struct IllegalRoutePreference;

impl std::fmt::Display for IllegalRoutePreference {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self)
    }
}

impl NetFlags {
    pub const ON_MESH: NetFlags = NetFlags(0b00000001);
    pub const DEFAULT_ROUTE: NetFlags = NetFlags(0b00000010);
    pub const CONFIGURE: NetFlags = NetFlags(0b00000100);
    pub const DHCP: NetFlags = NetFlags(0b00001000);
    pub const SLAAC_VALID: NetFlags = NetFlags(0b00010000);
    pub const SLAAC_PREFERRED: NetFlags = NetFlags(0b00100000);
    pub const PREF_LOW: NetFlags = NetFlags(0b11000000);
    pub const PREF_MED: NetFlags = NetFlags(0b00000000);
    pub const PREF_HIGH: NetFlags = NetFlags(0b01000000);

    const PREF_MASK: u8 = 0b11000000;
    const PREF_ILLEGAL: u8 = 0b10000000;

    pub fn try_new(raw_value: u8) -> Result<NetFlags, IllegalRoutePreference> {
        if (raw_value & Self::PREF_MASK) == Self::PREF_ILLEGAL {
            Err(IllegalRoutePreference)
        } else {
            Ok(NetFlags(raw_value))
        }
    }

    pub fn is_on_mesh(&self) -> bool {
        (self.0 & Self::ON_MESH.0) == Self::ON_MESH.0
    }

    pub fn is_default_route(&self) -> bool {
        (self.0 & Self::DEFAULT_ROUTE.0) == Self::DEFAULT_ROUTE.0
    }

    pub fn is_configure(&self) -> bool {
        (self.0 & Self::CONFIGURE.0) == Self::CONFIGURE.0
    }

    pub fn is_dhcp(&self) -> bool {
        (self.0 & Self::DHCP.0) == Self::DHCP.0
    }

    pub fn is_slaac_valid(&self) -> bool {
        (self.0 & Self::SLAAC_VALID.0) == Self::SLAAC_VALID.0
    }

    pub fn is_slaac_preferred(&self) -> bool {
        (self.0 & Self::SLAAC_PREFERRED.0) == Self::SLAAC_PREFERRED.0
    }

    pub fn route_preference(&self) -> Option<RoutePreference> {
        if self.is_default_route() {
            if (self.0 & Self::PREF_MASK) == Self::PREF_LOW.0 {
                Some(RoutePreference::Low)
            } else if (self.0 & Self::PREF_MASK) == Self::PREF_MED.0 {
                Some(RoutePreference::Medium)
            } else if (self.0 & Self::PREF_MASK) == Self::PREF_HIGH.0 {
                Some(RoutePreference::High)
            } else {
                panic!("Illegal Route Preference");
            }
        } else {
            None
        }
    }
}

impl std::fmt::Debug for NetFlags {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut first = true;

        if self.is_on_mesh() {
            write!(f, "ON_MESH")?;
            first = false;
        }
        if self.is_default_route() {
            if !first {
                write!(f, "|")?;
            }
            write!(f, "DEFAULT_ROUTE")?;
            first = false;
            match self.route_preference().unwrap() {
                RoutePreference::Low => write!(f, "|PREF_LOW")?,
                RoutePreference::Medium => write!(f, "|PREF_MED")?,
                RoutePreference::High => write!(f, "|PREF_HIGH")?,
            };
        }
        if self.is_configure() {
            if !first {
                write!(f, "|")?;
            }
            write!(f, "CONFIG")?;
            first = false;
        }
        if self.is_dhcp() {
            if !first {
                write!(f, "|")?;
            }
            write!(f, "DHCP")?;
            first = false;
        }
        if self.is_slaac_valid() {
            if !first {
                write!(f, "|")?;
            }
            write!(f, "SLAAC_VALID")?;
            first = false;
        }
        if self.is_slaac_preferred() {
            if !first {
                write!(f, "|")?;
            }
            write!(f, "SLAAC_PREFERRED")?;
        }
        Ok(())
    }
}

impl core::ops::BitOr for NetFlags {
    type Output = NetFlags;

    /// Returns the union of the two sets of flags.
    #[inline]
    fn bitor(self, other: NetFlags) -> NetFlags {
        NetFlags(self.0 | other.0)
    }
}

impl core::ops::BitOrAssign for NetFlags {
    /// Adds the set of flags.
    #[inline]
    fn bitor_assign(&mut self, other: NetFlags) {
        *self = *self | other;
    }
}

impl core::ops::BitXor for NetFlags {
    type Output = NetFlags;

    /// Returns the left flags, but with all the right flags toggled.
    #[inline]
    fn bitxor(self, other: NetFlags) -> NetFlags {
        NetFlags(self.0 ^ other.0)
    }
}

impl core::ops::BitXorAssign for NetFlags {
    /// Toggles the set of flags.
    #[inline]
    fn bitxor_assign(&mut self, other: NetFlags) {
        *self = *self ^ other;
    }
}

impl core::ops::BitAnd for NetFlags {
    type Output = NetFlags;

    /// Returns the intersection between the two sets of flags.
    #[inline]
    fn bitand(self, other: NetFlags) -> NetFlags {
        NetFlags(self.0 & other.0)
    }
}

impl core::ops::BitAndAssign for NetFlags {
    /// Disables all flags disabled in the set.
    #[inline]
    fn bitand_assign(&mut self, other: NetFlags) {
        *self = *self & other;
    }
}

impl core::ops::Sub for NetFlags {
    type Output = NetFlags;

    /// Returns the set difference of the two sets of flags.
    #[inline]
    fn sub(self, other: NetFlags) -> NetFlags {
        NetFlags(self.0 & !(other.0 & NetFlags::PREF_MASK))
    }
}

impl core::ops::SubAssign for NetFlags {
    /// Disables all flags enabled in the set.
    #[inline]
    fn sub_assign(&mut self, other: NetFlags) {
        *self = *self - other;
    }
}

impl core::ops::Not for NetFlags {
    type Output = NetFlags;

    /// Returns the complement of this set of flags.
    #[inline]
    fn not(self) -> NetFlags {
        NetFlags(!(self.0 & NetFlags::PREF_MASK))
    }
}

impl From<NetFlags> for u8 {
    fn from(x: NetFlags) -> Self {
        x.0
    }
}

impl TryFrom<u8> for NetFlags {
    type Error = IllegalRoutePreference;
    fn try_from(x: u8) -> Result<Self, Self::Error> {
        NetFlags::try_new(x)
    }
}

impl TryPackAs<u8> for NetFlags {
    fn pack_as_len(&self) -> std::io::Result<usize> {
        Ok(1)
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
        TryPackAs::<u8>::try_pack_as(&self.0, buffer)
    }
}

impl TryPack for NetFlags {
    fn pack_len(&self) -> io::Result<usize> {
        Ok(1)
    }

    fn try_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        TryPackAs::<u8>::try_pack_as(&self.0, buffer)
    }
}

impl<'a> TryUnpackAs<'a, u8> for NetFlags {
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        let id: u8 = TryUnpackAs::<u8>::try_unpack_as(iter)?;

        id.try_into().context(UnpackingError::InvalidValue)
    }
}

impl<'a> TryUnpack<'a> for NetFlags {
    type Unpacked = NetFlags;
    fn try_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
        TryUnpackAs::<u8>::try_unpack_as(iter)
    }
}

#[derive(Default, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct RouteFlags(u8);

impl RouteFlags {
    pub const PREF_LOW: RouteFlags = RouteFlags(0b11000000);
    pub const PREF_MED: RouteFlags = RouteFlags(0b00000000);
    pub const PREF_HIGH: RouteFlags = RouteFlags(0b01000000);

    const PREF_MASK: u8 = 0b11000000;
    const PREF_ILLEGAL: u8 = 0b10000000;

    pub fn try_new(raw_value: u8) -> Result<RouteFlags, IllegalRoutePreference> {
        if (raw_value & Self::PREF_MASK) == Self::PREF_ILLEGAL {
            Err(IllegalRoutePreference)
        } else {
            Ok(RouteFlags(raw_value))
        }
    }

    pub fn route_preference(&self) -> RoutePreference {
        if (self.0 & Self::PREF_MASK) == Self::PREF_LOW.0 {
            RoutePreference::Low
        } else if (self.0 & Self::PREF_MASK) == Self::PREF_MED.0 {
            RoutePreference::Medium
        } else if (self.0 & Self::PREF_MASK) == Self::PREF_HIGH.0 {
            RoutePreference::High
        } else {
            panic!("Illegal Route Preference");
        }
    }
}

impl std::fmt::Debug for RouteFlags {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.route_preference() {
            RoutePreference::Low => write!(f, "PREF_LOW")?,
            RoutePreference::Medium => write!(f, "PREF_MED")?,
            RoutePreference::High => write!(f, "PREF_HIGH")?,
        };
        Ok(())
    }
}

impl From<RouteFlags> for u8 {
    fn from(x: RouteFlags) -> Self {
        x.0
    }
}

impl TryFrom<u8> for RouteFlags {
    type Error = IllegalRoutePreference;
    fn try_from(x: u8) -> Result<Self, Self::Error> {
        RouteFlags::try_new(x)
    }
}

impl TryPackAs<u8> for RouteFlags {
    fn pack_as_len(&self) -> std::io::Result<usize> {
        Ok(1)
    }

    fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
        TryPackAs::<u8>::try_pack_as(&self.0, buffer)
    }
}

impl TryPack for RouteFlags {
    fn pack_len(&self) -> io::Result<usize> {
        Ok(1)
    }

    fn try_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
        TryPackAs::<u8>::try_pack_as(&self.0, buffer)
    }
}

impl<'a> TryUnpackAs<'a, u8> for RouteFlags {
    fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
        let id: u8 = TryUnpackAs::<u8>::try_unpack_as(iter)?;

        id.try_into().context(UnpackingError::InvalidValue)
    }
}

impl<'a> TryUnpack<'a> for RouteFlags {
    type Unpacked = RouteFlags;
    fn try_unpack(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self::Unpacked> {
        TryUnpackAs::<u8>::try_unpack_as(iter)
    }
}
