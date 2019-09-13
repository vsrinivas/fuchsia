// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod aes;
mod rc4;

use aes::NistAes;
use failure::Error;
use wlan_common::ie::rsn::akm::Akm;

/// An arbitrary algorithm used to encrypt the key data field of an EAPoL keyframe.
/// Usage is specified in IEEE 802.11-2016 8.5.2 j
pub trait Algorithm {
    /// Uses the given KEK and IV as a key to wrap the given data for secure transmission.
    fn wrap_key(&self, kek: &[u8], iv: &[u8; 16], data: &[u8]) -> Result<Vec<u8>, Error>;
    /// Uses the given KEK and IV as a key to unwrap the given data after secure transmission.
    fn unwrap_key(&self, kek: &[u8], iv: &[u8; 16], data: &[u8]) -> Result<Vec<u8>, Error>;
}

/// Returns the keywrap algorithm specified by IEEE 802.11-2016 for the given AKM.
pub fn keywrap_algorithm(akm: &Akm) -> Option<Box<dyn Algorithm>> {
    // IEEE 802.11-2016, 12.7.3, Table 12-8
    match akm.suite_type {
        1..=13 => Some(Box::new(NistAes)),
        _ => None,
    }
}
