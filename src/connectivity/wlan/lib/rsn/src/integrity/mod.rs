// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[allow(unused)]
pub mod hmac_md5;
pub mod hmac_sha1;

use crate::Error;
use hmac_md5::HmacMd5;
use hmac_sha1::HmacSha1;

pub trait Algorithm {
    fn verify(&self, key: &[u8], data: &[u8], expected: &[u8]) -> bool;
    fn compute(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error>;
}

/// IEEE Std 802.11-2016, 12.7.2 b.1)
pub fn integrity_algorithm(key_descriptor_version: u16) -> Option<Box<dyn Algorithm>> {
    match key_descriptor_version {
        1 => Some(Box::new(HmacMd5::new())),
        2 => Some(Box::new(HmacSha1::new())),
        // TODO(fxb/37359): Add support for the AES-128-CMAC integrity algorithm.
        _ => None,
    }
}
