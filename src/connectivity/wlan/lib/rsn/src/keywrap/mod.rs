// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod aes;

use aes::NistAes;
use failure::Error;
use wlan_common::ie::rsn::akm::Akm;

pub trait Algorithm {
    fn wrap(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error>;
    fn unwrap(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error>;
}

pub fn keywrap_algorithm(akm: &Akm) -> Option<Box<Algorithm>> {
    // IEEE 802.11-2016, 12.7.3, Table 12-8
    match akm.suite_type {
        1...13 => Some(Box::new(NistAes)),
        _ => None,
    }
}
