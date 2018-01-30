// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::Result;

pub const OUI: [u8; 3] = [0x00, 0x0F, 0xAC];

pub trait Factory<'a> {
    type Suite;

    fn new(oui: &'a [u8], suite_type: u8) -> Result<Self::Suite>;
}