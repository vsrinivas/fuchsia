// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Bytes per page
pub const PAGE_SIZE_BYTES: usize = 4096;

/// Size of the a VMO block header.
pub const HEADER_SIZE_BYTES: usize = 8;

/// Index of the virtual root NODE block (this block is *not* in the VMO).
pub const ROOT_INDEX: u32 = 0;

/// Index of the HEADER block.
pub const HEADER_INDEX: u32 = 0;

/// Magic number for the Header block. "INSP" in UTF-8 little-endian.
pub const HEADER_MAGIC_NUMBER: u32 = 0x50534e49;

/// Version number for the Header block.
pub const HEADER_VERSION_NUMBER: u32 = 1;

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

/// Default number of bytes for the VMO: 256K
pub const DEFAULT_VMO_SIZE_BYTES: usize = 256 * 1024;

/// Minimum size for the VMO: 4K
pub const MINIMUM_VMO_SIZE_BYTES: usize = 4 * 1024;

/// Length in bytes of metadata in the payload of an array block.
pub const ARRAY_PAYLOAD_METADATA_SIZE_BYTES: usize = 8;

/// Extra slots for a linear histogram: 2 parameter slots (floor, step size) and
/// 2 overflow slots.
pub const LINEAR_HISTOGRAM_EXTRA_SLOTS: usize = 4;

/// Extra slots for an exponential histogram: 3 parameter slots (floor, initial
/// step and step multiplier) and 2 overflow slots.
pub const EXPONENTIAL_HISTOGRAM_EXTRA_SLOTS: usize = 5;
