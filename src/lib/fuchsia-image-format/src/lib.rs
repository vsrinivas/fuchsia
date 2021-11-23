// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error};

mod image_format;
mod linux_drm;

pub use image_format::*;
pub use linux_drm::*;

pub fn round_up_to_increment(size: usize, increment: usize) -> Result<usize, Error> {
    let spare = size % increment;
    if spare > 0 {
        size.checked_add(increment - spare).ok_or_else(|| anyhow!("Overflow when adding to size."))
    } else {
        Ok(size)
    }
}
