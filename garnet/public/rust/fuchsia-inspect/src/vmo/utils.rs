// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::vmo::constants;
use num_traits::ToPrimitive;

/// Returns the smallest order such that (MIN_ORDER_SHIFT << order) >= size.
/// Size must be non zero.
pub fn fit_order(size: usize) -> usize {
    std::mem::size_of::<usize>() * 8
        - (size - 1).leading_zeros().to_usize().unwrap()
        - constants::MIN_ORDER_SHIFT
}

/// Get index in the VMO for a given |offset|.
pub fn index_for_offset(offset: usize) -> u32 {
    (offset / constants::MIN_ORDER_SIZE).to_u32().unwrap()
}

/// Get offset in the VMO for a given |index|.
pub fn offset_for_index(index: u32) -> usize {
    index.to_usize().unwrap() * constants::MIN_ORDER_SIZE
}

/// Get size in bytes of a given |order|.
pub fn order_to_size(order: usize) -> usize {
    constants::MIN_ORDER_SIZE << order
}
