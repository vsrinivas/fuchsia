// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::{Debug, Display, Error, Formatter};
use zerocopy::{AsBytes, FromBytes, LayoutVerified, Unaligned};

/// Error type indicating that the given slice was not the expected size.
#[derive(Debug, Eq, PartialEq, Hash, thiserror::Error)]
pub struct WrongSize;

impl Display for WrongSize {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), Error> {
        <Self as Debug>::fmt(self, f)
    }
}

/// Data type representing a EUI64 address.
#[derive(Debug, Eq, PartialEq, Hash, Copy, Clone, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub struct EUI64(pub [u8; 8]);

impl std::default::Default for EUI64 {
    fn default() -> Self {
        EUI64([0u8; 8])
    }
}

/// Converts a borrowed EUI64 into a borrowed byte slice.
impl<'a> std::convert::Into<&'a [u8]> for &'a EUI64 {
    fn into(self) -> &'a [u8] {
        &self.0
    }
}

/// Converts a borrowed byte slice into a borrowed EUI64 reference.
/// Will panic if the length of the slice is not exactly 8 bytes.
impl<'a> std::convert::TryInto<&'a EUI64> for &'a [u8] {
    type Error = WrongSize;

    fn try_into(self) -> Result<&'a EUI64, Self::Error> {
        LayoutVerified::<_, EUI64>::new_unaligned(self)
            .ok_or(WrongSize)
            .map(LayoutVerified::into_ref)
    }
}

/// Data type representing a EUI48 address.
#[derive(Debug, Eq, PartialEq, Hash, Copy, Clone, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub struct EUI48(pub [u8; 6]);

impl std::default::Default for EUI48 {
    fn default() -> Self {
        EUI48([0u8; 6])
    }
}

/// Converts a borrowed EUI48 into a borrowed byte slice.
impl<'a> std::convert::Into<&'a [u8]> for &'a EUI48 {
    fn into(self) -> &'a [u8] {
        &self.0
    }
}

/// Converts a borrowed byte slice into a borrowed EUI48 reference.
/// Will panic if the length of the slice is not exactly 6 bytes.
impl<'a> std::convert::TryInto<&'a EUI48> for &'a [u8] {
    type Error = WrongSize;

    fn try_into(self) -> Result<&'a EUI48, Self::Error> {
        LayoutVerified::<_, EUI48>::new_unaligned(self)
            .ok_or(WrongSize)
            .map(LayoutVerified::into_ref)
    }
}
