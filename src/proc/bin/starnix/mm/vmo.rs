// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::mm::PAGE_SIZE;

pub fn round_up_to_page_size(size: usize, page_size: usize) -> usize {
    let spare = size % page_size;
    if spare > 0 {
        size + (page_size - spare)
    } else {
        size
    }
}

pub fn round_up_to_system_page_size(size: usize) -> usize {
    round_up_to_page_size(size, *PAGE_SIZE as usize)
}
