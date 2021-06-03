// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{AsBytes, FromBytes};

use crate::types::*;

/// Matches iovec_t.
#[derive(Debug, Default, Clone, Copy, AsBytes, FromBytes)]
#[repr(C)]
pub struct UserBuffer {
    pub address: UserAddress,
    pub length: usize,
}

impl UserBuffer {
    pub fn get_total_length(buffers: &[UserBuffer]) -> usize {
        let mut total = 0;
        for buffer in buffers {
            total += buffer.length;
        }
        total
    }
}
