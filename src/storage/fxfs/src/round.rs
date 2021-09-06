// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub fn round_up<T: Into<u64>>(offset: u64, block_size: T) -> Option<u64> {
    let block_size = block_size.into();
    Some(round_down(offset.checked_add(block_size - 1)?, block_size))
}

pub fn round_down<T: Into<u64>>(offset: u64, block_size: T) -> u64 {
    offset - offset % block_size.into()
}
