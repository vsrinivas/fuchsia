// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use hex;
use ring::digest;

pub fn hash_data_and_expect(data: &[u8], expected_sha: &str) {
    let sha = digest::digest(&digest::SHA256, &data);
    assert_eq!(hex::encode(sha.as_ref()), expected_sha);
}

pub const TEST_PEM: &'static str = test_keys::TEST_RSA_4096_PEM;
pub const TEST_METADATA: &'static [u8] = "TEST_METADATA".as_bytes();
