// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub(crate) mod checksum;
pub(crate) mod records;

/// Whether `size` fits in a `u16`.
pub(crate) fn fits_in_u16(size: usize) -> bool {
    size < 1 << 16
}

/// Whether `size` fits in a `u32`.
pub(crate) fn fits_in_u32(size: usize) -> bool {
    // trivially true when usize is 32 bits wide
    cfg!(target_pointer_width = "32") || size < 1 << 32
}
