// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use vbmeta::Key;
use vbmeta::VBMeta;
use vbmeta::{HashDescriptor, Salt};

use std::convert::TryFrom;

const PEM: &str = include_str!(env!("AVB_KEY"));
const METADATA: &[u8] = include_bytes!(env!("AVB_METADATA"));
const SALT: &str = env!("SALT");
const IMAGE: &[u8] = include_bytes!(env!("IMAGE"));
const EXPECTED_VBMETA: &[u8] = include_bytes!(env!("EXPECTED_VBMETA"));

#[test]
fn avbtool_comparison() {
    let key = Key::try_new(PEM, METADATA).unwrap();

    let salt_bytes: &[u8] = &hex::decode(SALT).unwrap();
    let salt = Salt::try_from(salt_bytes).unwrap();
    let descriptor = HashDescriptor::new("zircon", IMAGE, salt);
    let descriptors = vec![descriptor];

    let vbmeta = VBMeta::sign(descriptors, key).unwrap();
    assert_eq!(vbmeta.as_bytes(), EXPECTED_VBMETA);
}
