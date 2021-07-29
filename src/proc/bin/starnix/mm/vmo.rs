// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::mm::PAGE_SIZE;

// TODO: Move this function to somewhere more generic. It doesn't really have
// anything to do with the memory manager.
pub fn round_up_to_increment(size: usize, increment: usize) -> usize {
    let spare = size % increment;
    if spare > 0 {
        size + (increment - spare)
    } else {
        size
    }
}

pub fn round_up_to_system_page_size(size: usize) -> usize {
    round_up_to_increment(size, *PAGE_SIZE as usize)
}
