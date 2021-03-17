// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::buffer_allocator::BufferAllocator, std::ops::Range};

// TODO(jfsulliv): Eventually we will want zero-copy buffers which are provided by filesystem
// clients (e.g. via zx::stream) and which we either splice pages into or out of from a transfer
// buffer, or which we directly connect to the block device, or which we read and write to in some
// different way (involving changes to the block interface).

// TODO(jfsulliv): Eventually we will want unmapped buffers, which is necessary in some cases
// (e.g. the source VMO given to zx_pager_supply_pages cannot be mapped). This would most likely
// be a new type.

/// Buffer is a buffer that is used for I/O with the block device.
/// These objects should be created from a BufferAllocator.
#[derive(Debug)]
pub struct Buffer<'a> {
    slice: &'a mut [u8],
    range: Range<usize>,
    allocator: Option<&'a BufferAllocator>,
}

impl<'a> Buffer<'a> {
    pub(super) fn new(
        slice: &'a mut [u8],
        range: Range<usize>,
        allocator: &'a BufferAllocator,
    ) -> Self {
        Self { slice, range, allocator: Some(allocator) }
    }
    /// Returns the buffer's capacity.
    pub fn size(&self) -> usize {
        self.slice.len()
    }

    /// Returns a slice of the buffer's contents.
    pub fn as_slice(&self) -> &[u8] {
        &*self.slice
    }

    /// Returns a mutable slice of the buffer's contents.
    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        self.slice
    }

    /// Returns the range in the underlying BufferSource this Buffer covers.
    pub(super) fn range(&self) -> &Range<usize> {
        &self.range
    }

    unsafe fn empty() -> Self {
        Self {
            slice: std::slice::from_raw_parts_mut(std::ptr::null_mut::<u8>(), 0),
            range: 0..0,
            allocator: None,
        }
    }
}

impl<'a> Drop for Buffer<'a> {
    fn drop(&mut self) {
        let allocator = self.allocator;
        self.allocator = None;
        if let Some(allocator) = allocator {
            allocator.take_buffer(std::mem::replace(self, unsafe { Self::empty() }));
        }
    }
}
