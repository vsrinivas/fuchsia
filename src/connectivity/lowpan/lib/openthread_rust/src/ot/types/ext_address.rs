// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Data type representing a EUI64 address.
/// Functional equivalent of [`otsys::otExtAddress`](crate::otsys::otExtAddress).
#[derive(Default, Copy, Clone)]
#[repr(transparent)]
pub struct ExtAddress(pub otExtAddress);

impl_ot_castable!(ExtAddress, otExtAddress);

impl std::fmt::Debug for ExtAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "ExtAddress({})", hex::encode(self.as_slice()))
    }
}

impl ExtAddress {
    /// Tries to create an `ExtAddress` reference from a byte slice.
    pub fn try_ref_from_slice(slice: &[u8]) -> Result<&ExtAddress, ot::WrongSize> {
        if slice.len() == OT_EXT_ADDRESS_SIZE as usize {
            Ok(unsafe { Self::ref_from_ot_ptr((slice as *const [u8]) as *const otExtAddress) }
                .unwrap())
        } else {
            Err(ot::WrongSize)
        }
    }

    /// Returns the Extended Address as a byte slice.
    pub fn as_slice(&self) -> &[u8] {
        &self.0.m8
    }

    /// Creates a `Vec<u8>` from this Extended Address.
    pub fn to_vec(&self) -> Vec<u8> {
        self.as_slice().to_vec()
    }
}
