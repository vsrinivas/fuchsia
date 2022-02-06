// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Network Key.
/// Functional equivalent of [`otsys::otNetworkKey`](crate::otsys::otNetworkKey).
#[derive(Default, Copy, Clone)]
#[repr(transparent)]
pub struct NetworkKey(pub otNetworkKey);

impl_ot_castable!(NetworkKey, otNetworkKey);

impl std::fmt::Debug for NetworkKey {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "NetworkKey({})", hex::encode(self.as_slice()))
    }
}

impl NetworkKey {
    /// Returns the network key as a byte slice.
    pub fn as_slice(&self) -> &[u8] {
        &self.0.m8
    }

    /// Creates a `Vec<u8>` from this network key.
    pub fn to_vec(&self) -> Vec<u8> {
        self.as_slice().to_vec()
    }
}

impl From<[u8; OT_NETWORK_KEY_SIZE as usize]> for NetworkKey {
    fn from(key: [u8; OT_NETWORK_KEY_SIZE as usize]) -> Self {
        Self(otNetworkKey { m8: key })
    }
}

impl From<NetworkKey> for [u8; OT_NETWORK_KEY_SIZE as usize] {
    fn from(key: NetworkKey) -> Self {
        key.0.m8
    }
}

impl NetworkKey {
    /// Tries to create a `NetworkKey` reference from a byte slice.
    pub fn try_ref_from_slice(slice: &[u8]) -> Result<&NetworkKey, ot::WrongSize> {
        if slice.len() == OT_NETWORK_KEY_SIZE as usize {
            Ok(unsafe { Self::ref_from_ot_ptr((slice as *const [u8]) as *const otNetworkKey) }
                .unwrap())
        } else {
            Err(ot::WrongSize)
        }
    }
}

/// PSKC. Functional equivalent of [`otsys::otPskc`](crate::otsys::otPskc).
///
/// Used to establish the Commissioner Session.
#[derive(Default, Copy, Clone)]
#[repr(transparent)]
pub struct Pskc(pub otPskc);

impl_ot_castable!(Pskc, otPskc);

impl std::fmt::Debug for Pskc {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Pskc({:?})", hex::encode(self.as_slice()))
    }
}

impl Pskc {
    /// Returns the PSKC as a byte slice.
    pub fn as_slice(&self) -> &[u8] {
        &self.0.m8
    }

    /// Creates a `Vec<u8>` from this PSKC
    pub fn to_vec(&self) -> Vec<u8> {
        self.as_slice().to_vec()
    }
}

impl From<[u8; OT_PSKC_MAX_SIZE as usize]> for Pskc {
    fn from(key: [u8; OT_PSKC_MAX_SIZE as usize]) -> Self {
        Self(otPskc { m8: key })
    }
}

impl From<Pskc> for [u8; OT_PSKC_MAX_SIZE as usize] {
    fn from(key: Pskc) -> Self {
        key.0.m8
    }
}

impl Pskc {
    /// Tries to create a `Pskc` reference from a byte slice.
    pub fn try_ref_from_slice(slice: &[u8]) -> Result<&Pskc, ot::WrongSize> {
        if slice.len() == OT_PSKC_MAX_SIZE as usize {
            Ok(unsafe { Self::ref_from_ot_ptr((slice as *const [u8]) as *const otPskc) }.unwrap())
        } else {
            Err(ot::WrongSize)
        }
    }
}
