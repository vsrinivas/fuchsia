// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Data type representing an extended PAN-ID.
/// Functional equivalent of [`otsys::otExtendedPanId`](crate::otsys::otExtendedPanId).
#[derive(Default, Copy, Clone)]
#[repr(transparent)]
pub struct ExtendedPanId(pub otExtendedPanId);

impl_ot_castable!(ExtendedPanId, otExtendedPanId);

impl std::fmt::Debug for ExtendedPanId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "ExtendedPanId({})", hex::encode(self.as_slice()))
    }
}

impl ExtendedPanId {
    /// Tries to create an `ExtendedPanId` reference from a byte slice.
    pub fn try_ref_from_slice(slice: &[u8]) -> Result<&ExtendedPanId, ot::WrongSize> {
        if slice.len() == OT_EXT_PAN_ID_SIZE as usize {
            Ok(unsafe { Self::ref_from_ot_ptr((slice as *const [u8]) as *const otExtendedPanId) }
                .unwrap())
        } else {
            Err(ot::WrongSize)
        }
    }

    /// Returns this Extended PAN-ID as a byte slice.
    pub fn as_slice(&self) -> &[u8] {
        &self.0.m8
    }

    /// Creates a `Vec<u8>` from this Extended PAN-ID.
    pub fn to_vec(&self) -> Vec<u8> {
        self.as_slice().to_vec()
    }
}
