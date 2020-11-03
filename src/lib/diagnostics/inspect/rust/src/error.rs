// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::format::block_type::BlockType;
use anyhow;
use fuchsia_zircon as zx;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("FIDL error")]
    Fidl(#[source] anyhow::Error),

    #[error("Failed to allocate vmo")]
    AllocateVmo(#[source] zx::Status),

    #[error("Failed to free {value_type} index={index}")]
    Free {
        value_type: &'static str,
        index: u32,
        #[source]
        error: Box<Error>,
    },

    #[error("Failed to create {value_type}")]
    Create {
        value_type: &'static str,
        #[source]
        error: Box<Error>,
    },

    #[error("{0} is no-op")]
    NoOp(&'static str),

    #[error("Failed to create the internal heap")]
    CreateHeap(#[source] Box<Error>),

    #[error("Failed to create the internal state")]
    CreateState(#[source] Box<Error>),

    #[error("Attempted to free a FREE block at index {0}")]
    BlockAlreadyFree(u32),

    #[error("Invalid index {0}: {1}")]
    InvalidIndex(u32, &'static str),

    #[error("Heap already at its maximum size")]
    HeapMaxSizeReached,

    #[error("Cannot allocate block of size {0}. Exceeds maximum.")]
    BlockSizeTooBig(usize),

    #[error("Invalid block type at index {0}: {1:?}")]
    InvalidBlockType(usize, BlockType),

    #[error("Invalid block type at index {0}: {1}")]
    InvalidBlockTypeNumber(u32, u8),

    #[error("Array index out of bounds: {0}")]
    ArrayIndexOutOfBounds(usize),

    #[error("Invalid block type. Expected: {0}, actual: {1}")]
    UnexpectedBlockType(BlockType, BlockType),

    #[error("Invalid block type. Expected: {0}, got: {1}")]
    UnexpectedBlockTypeRepr(&'static str, BlockType),

    #[error("Expected lock state locked={0}")]
    ExpectedLockState(bool),

    #[error("Invalid order {0}")]
    InvalidBlockOrder(usize),

    #[error("Invalid order {0} at index {1}")]
    InvalidBlockOrderAtIndex(usize, u32),

    #[error("Cannot swap blocks of different order or container")]
    InvalidBlockSwap,

    #[error("Expected a numeric entry type for the array at index {0}")]
    NonNumericArrayType(u32),

    #[error("{slots} exceeds the maximum number of slots for order {order}: {max_capacity}")]
    ArrayCapacityExceeded { slots: usize, order: usize, max_capacity: usize },

    #[error("Invalid {value_type} flags={flags} at index {index}")]
    InvalidFlags { value_type: &'static str, flags: u8, index: u32 },

    #[error("Name is not utf8")]
    NameNotUtf8,

    #[error("Failed to convert array slots to usize")]
    FailedToConvertArraySlotsToUsize,
}

impl Error {
    pub fn free(value_type: &'static str, index: u32, error: Error) -> Self {
        Self::Free { value_type, index, error: Box::new(error) }
    }

    pub fn create(value_type: &'static str, error: Error) -> Self {
        Self::Create { value_type, error: Box::new(error) }
    }

    pub fn invalid_index(index: u32, reason: &'static str) -> Self {
        Self::InvalidIndex(index, reason)
    }

    pub fn invalid_flags(value_type: &'static str, flags: u8, index: u32) -> Self {
        Self::InvalidFlags { value_type, flags, index }
    }

    pub fn array_capacity_exceeded(slots: usize, order: usize, max_capacity: usize) -> Self {
        Self::ArrayCapacityExceeded { slots, order, max_capacity }
    }
}
