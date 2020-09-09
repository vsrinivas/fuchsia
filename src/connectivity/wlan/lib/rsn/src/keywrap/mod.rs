// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod aes;
mod rc4;

use crate::Error;

use wlan_common::ie::rsn::akm;
use {aes::NistAes, rc4::Rc4};

/// An arbitrary algorithm used to encrypt the key data field of an EAPoL keyframe.
/// Usage is specified in IEEE 802.11-2016 8.5.2 j
pub trait Algorithm {
    /// Uses the given KEK and IV as a key to wrap the given data for secure transmission.
    fn wrap_key(&self, kek: &[u8], iv: &[u8; 16], data: &[u8]) -> Result<Vec<u8>, Error>;
    /// Uses the given KEK and IV as a key to unwrap the given data after secure transmission.
    fn unwrap_key(&self, kek: &[u8], iv: &[u8; 16], data: &[u8]) -> Result<Vec<u8>, Error>;
}

/// IEEE Std 802.11-2016, 12.7.2 b.1)
pub fn keywrap_algorithm(
    key_descriptor_version: u16,
    akm: &akm::Akm,
) -> Option<Box<dyn Algorithm>> {
    match key_descriptor_version {
        1 => Some(Box::new(Rc4)),
        2 => Some(Box::new(NistAes)),
        0 if akm.suite_type == akm::SAE => Some(Box::new(NistAes)),
        _ => None,
    }
}
