// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO: We don't seem to have a good way to query this at runtime, and multiple programs/libraries
// define their own constant. Needs to be consistent with kernel definitions.
pub const PAGE_SIZE: usize = 4096;

/// Returns the starting address of the page that contains this address. For example, if page size
/// is 0x1000, page_start(0x3001) == page_start(0x3FAB) == 0x3000.
pub fn page_start(addr: usize) -> usize {
    addr & !(PAGE_SIZE - 1)
}

/// Returns the offset of the address within its page. For example, if page size is 0x1000,
/// page_offset(0x2ABC) == page_offset(0x5ABC) == 0xABC.
pub fn page_offset(addr: usize) -> usize {
    addr & (PAGE_SIZE - 1)
}

/// Returns starting address of the next page after the one that contains this address, unless
/// address is already page aligned. For example, if page size is 0x1000, page_end(0x4001) ==
/// page_end(0x4FFF) == 0x5000, but page_end(0x4000) == 0x4000.
pub fn page_end(addr: usize) -> usize {
    page_start(addr + PAGE_SIZE - 1)
}
