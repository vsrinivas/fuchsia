// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Bytes per page
pub const PAGE_SIZE_BYTES: usize = 4096;

/// Size of the a VMO block header.
pub const HEADER_SIZE_BYTES: usize = 8;

/// Index of the root NODE block.
pub const ROOT_PARENT_INDEX: u32 = 0;

/// Index of the HEADER block.
pub const HEADER_INDEX: u32 = 0;

/// Magic number for the Header block. "INSP" in UTF-8.
pub const HEADER_MAGIC_NUMBER: u32 = 0x494e5350;

/// Version number for the Header block.
pub const HEADER_VERSION_NUMBER: u32 = 0;

/// Maximum number order of a block.
pub const NUM_ORDERS: usize = 8;

/// The shift for order 0.
pub const MIN_ORDER_SHIFT: usize = 4;

/// The size for order 0.
pub const MIN_ORDER_SIZE: usize = 1 << MIN_ORDER_SHIFT; // 16 bytes

/// The shift for order NUM_ORDERS-1 (the maximum order)
pub const MAX_ORDER_SHIFT: usize = MIN_ORDER_SHIFT + NUM_ORDERS - 1;

/// The size for order NUM_ORDERS-1 (the maximum order)
pub const MAX_ORDER_SIZE: usize = 1 << MAX_ORDER_SHIFT;

/// Property format flag for a bytevector.
pub const PROPERTY_FLAG_BYTE_VECTOR: u8 = 1;

/// Property format flag for a string.
pub const PROPERTY_FLAG_STRING: u8 = 0;
