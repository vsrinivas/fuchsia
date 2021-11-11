// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::block_type::BlockType;

/// Errors that Inspect API functions can return.
#[derive(Clone, Debug, thiserror::Error)]
pub enum Error {
    #[error("{slots} exceeds the maximum number of slots for order {order}: {max_capacity}")]
    ArrayCapacityExceeded { slots: usize, order: usize, max_capacity: usize },

    #[error("Array index out of bounds: {0}")]
    ArrayIndexOutOfBounds(usize),

    #[error("Expected lock state locked={0}")]
    ExpectedLockState(bool),

    #[error("Invalid order {0}")]
    InvalidBlockOrder(usize),

    #[error("Cannot swap blocks of different order or container")]
    InvalidBlockSwap,

    #[error("Invalid block type at index {0}: {1}")]
    InvalidBlockTypeNumber(u32, u8),

    #[error("Invalid {value_type} flags={flags} at index {index}")]
    InvalidFlags { value_type: &'static str, flags: u8, index: u32 },

    #[error("Failed to convert array slots to usize")]
    FailedToConvertArraySlotsToUsize,

    #[error("Name is not utf8")]
    NameNotUtf8,

    #[error("Expected a valid entry type for the array at index {0}")]
    InvalidArrayType(u32),

    #[error("Invalid block type. Expected: {0}, actual: {1}")]
    UnexpectedBlockType(BlockType, BlockType),

    #[error("Invalid block type. Expected: {0}, got: {1}")]
    UnexpectedBlockTypeRepr(String, BlockType),

    #[error("Invalid reference count. Reference count must be in range (0, 2^32)")]
    InvalidReferenceCount,
}

impl Error {
    pub fn array_capacity_exceeded(slots: usize, order: usize, max_capacity: usize) -> Self {
        Self::ArrayCapacityExceeded { slots, order, max_capacity }
    }

    pub fn invalid_flags(value_type: &'static str, flags: u8, index: u32) -> Self {
        Self::InvalidFlags { value_type, flags, index }
    }
}
