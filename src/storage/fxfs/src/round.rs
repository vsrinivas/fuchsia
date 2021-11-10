// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Round `offset` up to next multiple of `block_size`.
/// This function will fail if rounding up leads to an integer overflow.
///
/// (Note that unstable rust is currently adding the same function
/// `{integer}::checked_next_multiple_of()` behind the "int_roundings" feature.)
pub fn round_up<T: Into<u64>>(offset: u64, block_size: T) -> Option<u64> {
    let block_size = block_size.into();
    Some(round_down(offset.checked_add(block_size - 1)?, block_size))
}

/// Round `offset` down to the previous multiple of `block_size`.
pub fn round_down<T: Into<u64>>(offset: u64, block_size: T) -> u64 {
    let block_size = block_size.into();
    offset - offset % block_size
}
