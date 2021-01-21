// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod cmac_aes128;
#[allow(unused)]
pub mod hmac_md5;
pub mod hmac_sha1;

use {
    crate::{
        integrity::{cmac_aes128::CmacAes128, hmac_md5::HmacMd5, hmac_sha1::HmacSha1},
        Error,
    },
    mundane::bytes,
    wlan_common::ie::rsn::akm,
};

pub trait Algorithm {
    // NOTE: The default implementation truncates the output if it is larger than the given
    //       expected bytes.
    fn verify(&self, key: &[u8], data: &[u8], expected: &[u8]) -> bool {
        self.compute(key, data)
            .map(|mut output| {
                output.resize(expected.len(), 0);
                bytes::constant_time_eq(&output, expected)
            })
            .unwrap_or(false)
    }

    fn compute(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error>;
}

/// IEEE Std 802.11-2016, 12.7.2 b.1)
pub fn integrity_algorithm(
    key_descriptor_version: u16,
    akm: &akm::Akm,
) -> Option<Box<dyn Algorithm>> {
    match key_descriptor_version {
        1 => Some(Box::new(HmacMd5::new())),
        2 => Some(Box::new(HmacSha1::new())),
        // IEEE Std 802.11 does not specify a key descriptor version for SAE. In practice, 0 is used.
        3 | 0 if akm.suite_type == akm::SAE => Some(Box::new(CmacAes128::new())),
        _ => None,
    }
}
