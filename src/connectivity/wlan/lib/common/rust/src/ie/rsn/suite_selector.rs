// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::Bytes;
use failure;

pub const OUI: [u8; 3] = [0x00, 0x0F, 0xAC];

pub trait Factory {
    type Suite;

    fn new(oui: Bytes, suite_type: u8) -> Result<Self::Suite, failure::Error>;
}
