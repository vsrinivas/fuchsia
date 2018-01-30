// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Error, Result};

pub type Pmkid<'a> = &'a [u8];

pub fn new<'a>(pmkid: &'a [u8]) -> Result<Pmkid> {
    if pmkid.len() != 16 {
        Err(Error::InvalidPmkidLength(pmkid.len()))
    } else {
        Ok(pmkid)
    }
}