// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Definitions for all the block types.

use {
    num_derive::{FromPrimitive, ToPrimitive},
    std::fmt,
};

/// The type of a block.
#[derive(Debug, Clone, Eq, Ord, PartialEq, PartialOrd, FromPrimitive, ToPrimitive)]
pub enum BlockType {
    /// Contains index of the next free block of the same order.
    Free = 0,

    /// Available to be changed to a different class. Transitonal.
    Reserved = 1,

    /// One header at the beginning of the VMO region. Index 0.
    Header = 2,

    /// An entry in the tree, which might hold nodes, metrics or properties.
    /// Contains a reference count.
    NodeValue = 3,

    /// Numeric properties.
    IntValue = 4,
    UintValue = 5,
    DoubleValue = 6,

    /// String or bytevector property value.
    BufferValue = 7,

    /// Contains a string payload.
    Extent = 8,

    /// Gives blocks a human-readable identifier.
    Name = 9,

    /// A deleted object
    Tombstone = 10,

    /// An array value
    ArrayValue = 11,

    /// A link value
    LinkValue = 12,

    /// A boolean value
    BoolValue = 13,
}

impl fmt::Display for BlockType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match *self {
            BlockType::Free => write!(f, "FREE"),
            BlockType::Reserved => write!(f, "RESERVED"),
            BlockType::Header => write!(f, "HEADER"),
            BlockType::NodeValue => write!(f, "NODE_VALUE"),
            BlockType::IntValue => write!(f, "INT_VALUE"),
            BlockType::UintValue => write!(f, "UINT_VALUE"),
            BlockType::DoubleValue => write!(f, "DOUBLE_VALUE"),
            BlockType::BufferValue => write!(f, "BUFFER_VALUE"),
            BlockType::Extent => write!(f, "EXTENT"),
            BlockType::Name => write!(f, "NAME"),
            BlockType::Tombstone => write!(f, "TOMBSTONE"),
            BlockType::ArrayValue => write!(f, "ARRAY_VALUE"),
            BlockType::LinkValue => write!(f, "LINK_VALUE"),
            BlockType::BoolValue => write!(f, "BOOL_VALUE"),
        }
    }
}

impl BlockType {
    /// Returns whether the type is for a `*_VALUE` block or not.
    pub fn is_any_value(&self) -> bool {
        match *self {
            BlockType::NodeValue
            | BlockType::IntValue
            | BlockType::UintValue
            | BlockType::DoubleValue
            | BlockType::BufferValue
            | BlockType::ArrayValue
            | BlockType::LinkValue
            | BlockType::BoolValue => true,
            _ => false,
        }
    }

    /// Returns whether the type is of a numeric `*_VALUE` block or not.
    pub fn is_numeric_value(&self) -> bool {
        match *self {
            BlockType::IntValue | BlockType::UintValue | BlockType::DoubleValue => true,
            _ => false,
        }
    }

    /// Returns whether the type is node or tombstone or not.
    pub fn is_node_or_tombstone(&self) -> bool {
        match *self {
            BlockType::NodeValue | BlockType::Tombstone => true,
            _ => false,
        }
    }

    /// Returns an array of all the types.
    #[cfg(test)]
    pub fn all() -> [BlockType; 14] {
        [
            BlockType::Free,
            BlockType::Reserved,
            BlockType::Header,
            BlockType::NodeValue,
            BlockType::IntValue,
            BlockType::UintValue,
            BlockType::DoubleValue,
            BlockType::BufferValue,
            BlockType::Extent,
            BlockType::Name,
            BlockType::Tombstone,
            BlockType::ArrayValue,
            BlockType::LinkValue,
            BlockType::BoolValue,
        ]
    }
}
