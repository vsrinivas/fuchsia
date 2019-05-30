// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod hmac_sha1;

use crate::Error;
use hmac_sha1::HmacSha1;
use wlan_common::ie::rsn::akm::Akm;

pub trait Algorithm {
    fn verify(&self, key: &[u8], data: &[u8], expected: &[u8]) -> bool;
    fn compute(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error>;
}

pub fn integrity_algorithm(akm: &Akm) -> Option<Box<Algorithm>> {
    // IEEE 802.11-2016, 12.7.3, Table 12-8
    match akm.suite_type {
        1 | 2 => Some(Box::new(HmacSha1::new())),
        // TODO(hahnr): Add remaining integrity algorithms.
        3...13 => None,
        _ => None,
    }
}
