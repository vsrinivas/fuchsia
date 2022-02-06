// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

use core::cmp::Ordering;
use core::fmt::{Debug, Formatter};

/// Network Name.
/// Functional equivalent of [`otsys::otNetworkName`](crate::otsys::otNetworkName).
#[derive(Default, Copy, Clone)]
#[repr(transparent)]
pub struct NetworkName(pub otNetworkName);

impl_ot_castable!(NetworkName, otNetworkName);

impl Debug for NetworkName {
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        match self.try_as_str() {
            Ok(s) => s.fmt(f),
            Err(_) => write!(f, "[{:?}]", hex::encode(self.as_slice())),
        }
    }
}

impl PartialEq for NetworkName {
    fn eq(&self, other: &Self) -> bool {
        self.as_slice() == other.as_slice()
    }
}

impl Eq for NetworkName {}

impl PartialOrd for NetworkName {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        self.as_slice().partial_cmp(other.as_slice())
    }
}

impl Ord for NetworkName {
    fn cmp(&self, other: &Self) -> Ordering {
        self.as_slice().cmp(other.as_slice())
    }
}

impl NetworkName {
    /// Tries to create a network name instance from the given byte slice.
    pub fn try_from_slice(slice: &[u8]) -> Result<Self, ot::WrongSize> {
        use std::mem::transmute;

        let len = slice.len();
        if len > OT_NETWORK_NAME_MAX_SIZE as usize {
            return Err(ot::WrongSize);
        }

        sa::assert_eq_size!(u8, ::std::os::raw::c_char);

        // SAFETY: Casting signed bytes to unsigned bytes is defined behavior.
        let slice = unsafe { transmute::<&[u8], &[::std::os::raw::c_char]>(slice) };

        let mut ret = NetworkName::default();
        ret.0.m8[0..len].clone_from_slice(slice);

        Ok(ret)
    }

    /// Returns length of the network name in bytes. 0-16.
    pub fn len(&self) -> usize {
        self.0.m8.iter().position(|&x| x == 0).unwrap_or(OT_NETWORK_NAME_MAX_SIZE as usize)
    }

    /// Returns the network name as a byte slice with no trailing zeros.
    pub fn as_slice(&self) -> &[u8] {
        use std::mem::transmute;
        sa::assert_eq_size!(u8, ::std::os::raw::c_char);

        unsafe {
            // SAFETY: Casting signed bytes to unsigned bytes is defined behavior.
            transmute::<&[::std::os::raw::c_char], &[u8]>(&self.0.m8[0..self.len()])
        }
    }

    /// Creates a `Vec<u8>` from the raw bytes of this network name.
    pub fn to_vec(&self) -> Vec<u8> {
        self.as_slice().to_vec()
    }

    /// Tries to return a representation of this network name as a string slice.
    pub fn try_as_str(&self) -> Result<&str, std::str::Utf8Error> {
        std::str::from_utf8(self.as_slice())
    }

    /// Returns as a c-string pointer
    pub fn as_c_str(&self) -> *const ::std::os::raw::c_char {
        self.0.m8.as_ptr()
    }
}

impl<'a> TryFrom<&'a str> for NetworkName {
    type Error = ot::WrongSize;

    fn try_from(value: &'a str) -> Result<Self, Self::Error> {
        NetworkName::try_from_slice(value.as_bytes())
    }
}

impl<'a> TryFrom<&'a [u8]> for NetworkName {
    type Error = ot::WrongSize;

    fn try_from(value: &'a [u8]) -> Result<Self, Self::Error> {
        NetworkName::try_from_slice(value)
    }
}

impl<'a> TryFrom<Vec<u8>> for NetworkName {
    type Error = ot::WrongSize;

    fn try_from(value: Vec<u8>) -> Result<Self, Self::Error> {
        NetworkName::try_from_slice(&value)
    }
}

impl From<&NetworkName> for otNetworkName {
    fn from(x: &NetworkName) -> Self {
        x.as_ot_ref().clone()
    }
}
