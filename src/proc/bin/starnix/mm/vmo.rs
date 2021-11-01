// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errno;
use crate::mm::PAGE_SIZE;
use crate::types::*;

// TODO: Move this function to somewhere more generic. It doesn't really have
// anything to do with the memory manager.
pub fn round_up_to_increment(size: usize, increment: usize) -> Result<usize, Errno> {
    let spare = size % increment;
    if spare > 0 {
        size.checked_add(increment - spare).ok_or_else(|| errno!(EINVAL))
    } else {
        Ok(size)
    }
}

pub fn round_up_to_system_page_size(size: usize) -> Result<usize, Errno> {
    round_up_to_increment(size, *PAGE_SIZE as usize)
}
