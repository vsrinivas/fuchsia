// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///  Each increment of |index| in the VMO.
pub const BYTES_PER_INDEX: u32 = 16;

/// Bytes per page
pub const PAGE_SIZE_BYTES: u32 = 4096;

/// Size of the a VMO block header.
pub const HEADER_SIZE_BYTES: u32 = 8;

/// Index of the root NODE block.
pub const ROOT_PARENT_INDEX: u32 = 0;

/// Invalid free index to indicate we either are out of space or we are out of
/// free blocks.
pub const INVALID_INDEX: u32 = 0;

/// First index available for the heap.
pub const HEAP_START_INDEX: u32 = 4;

/// Default block order
pub const DEFAULT_BLOCK_ORDER: u8 = 0;

/// Magic number for the Header block. "INSP" in UTF-8.
pub const HEADER_MAGIC_NUMBER: u32 = 0x494e5350;

/// Version number for the Header block.
pub const HEADER_VERSION_NUMBER: u32 = 0;
