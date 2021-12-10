// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants;
use crate::BlockType;
use num_traits::ToPrimitive;
use std::cmp::{max, min};

/// Returns the smallest order such that (MIN_ORDER_SHIFT << order) >= size.
/// Size must be non zero.
pub fn fit_order(size: usize) -> usize {
    (std::mem::size_of::<usize>() * 8 - (size - 1).leading_zeros().to_usize().unwrap())
        .checked_sub(constants::MIN_ORDER_SHIFT)
        .unwrap_or(0)
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

/// Get the necessary |block size| to fit the given |payload_size| in range
/// MIN_ORDER_SIZE <= block size <= MAX_ORDER_SIZE
pub fn block_size_for_payload(payload_size: usize) -> usize {
    min(
        constants::MAX_ORDER_SIZE,
        max(payload_size + constants::HEADER_SIZE_BYTES, constants::MIN_ORDER_SIZE),
    )
}

/// Get the size in bytes for the payload section of a block of the given |order|.
pub fn payload_size_for_order(order: usize) -> usize {
    order_to_size(order) - constants::HEADER_SIZE_BYTES
}

/// Get the array capacity size for the given |order|.
pub fn array_capacity(order: usize, entry_type: BlockType) -> Option<usize> {
    entry_type.array_element_size().map(|size| {
        (order_to_size(order)
            - constants::HEADER_SIZE_BYTES
            - constants::ARRAY_PAYLOAD_METADATA_SIZE_BYTES)
            / size
    })
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn fit_order_test() {
        assert_eq!(0, fit_order(1));
        assert_eq!(0, fit_order(16));
        assert_eq!(1, fit_order(17));
        assert_eq!(2, fit_order(33));
        assert_eq!(7, fit_order(2048));
    }

    #[test]
    fn order_to_size_test() {
        assert_eq!(16, order_to_size(0));
        assert_eq!(32, order_to_size(1));
        assert_eq!(64, order_to_size(2));
        assert_eq!(128, order_to_size(3));
        assert_eq!(256, order_to_size(4));
        assert_eq!(512, order_to_size(5));
        assert_eq!(1024, order_to_size(6));
        assert_eq!(2048, order_to_size(7));
    }

    #[test]
    fn fit_payload_test() {
        for payload_size in 0..500 {
            let block_size = block_size_for_payload(payload_size);
            let order = fit_order(block_size);
            let payload_max = payload_size_for_order(order);
            assert!(
                payload_size <= payload_max,
                "Needed {} bytes for a payload, but only got {}; block size {}, order {}",
                payload_size,
                payload_max,
                block_size,
                order
            );
        }
    }

    #[test]
    fn array_capacity_numeric() {
        assert_eq!(2, array_capacity(1, BlockType::IntValue).expect("get size"));
        assert_eq!(2 + 4, array_capacity(2, BlockType::IntValue).expect("get size"));
        assert_eq!(2 + 4 + 8, array_capacity(3, BlockType::IntValue).expect("get size"));
        assert_eq!(2 + 4 + 8 + 16, array_capacity(4, BlockType::IntValue).expect("get size"));
        assert_eq!(2 + 4 + 8 + 16 + 32, array_capacity(5, BlockType::IntValue).expect("get size"));
        assert_eq!(
            2 + 4 + 8 + 16 + 32 + 64,
            array_capacity(6, BlockType::IntValue).expect("get size")
        );
        assert_eq!(
            2 + 4 + 8 + 16 + 32 + 64 + 128,
            array_capacity(7, BlockType::IntValue).expect("get size")
        );
    }

    #[test]
    fn array_capacity_string_reference() {
        assert_eq!(4, array_capacity(1, BlockType::StringReference).expect("get size"));
        assert_eq!(4 + 8, array_capacity(2, BlockType::StringReference).expect("get size"));
        assert_eq!(4 + 8 + 16, array_capacity(3, BlockType::StringReference).expect("get size"));
        assert_eq!(
            4 + 8 + 16 + 32,
            array_capacity(4, BlockType::StringReference).expect("get size")
        );
        assert_eq!(
            4 + 8 + 16 + 32 + 64,
            array_capacity(5, BlockType::StringReference).expect("get size")
        );
        assert_eq!(
            4 + 8 + 16 + 32 + 64 + 128,
            array_capacity(6, BlockType::StringReference).expect("get size")
        );
        assert_eq!(
            4 + 8 + 16 + 32 + 64 + 128 + 256,
            array_capacity(7, BlockType::StringReference).expect("get size")
        );
    }
}
