// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {self::buffer::Buffer, anyhow::Error, async_trait::async_trait};

pub mod buffer;
pub mod buffer_allocator;

#[cfg(target_os = "fuchsia")]
pub mod block_device;

#[async_trait]
/// Device is an abstract representation of an underlying block device.
pub trait Device: Send + Sync {
    /// Allocates a transfer buffer of at least |size| bytes for doing I/O with the device.
    /// The actual size of the buffer will be rounded up to a block-aligned size.
    fn allocate_buffer(&self, size: usize) -> Buffer<'_>;
    /// Returns the block size of the device. Buffers are aligned to block-aligned chunks.
    fn block_size(&self) -> u32;
    /// Fills |buffer| with blocks read from |offset|.
    async fn read(&self, offset: u64, buffer: &mut Buffer<'_>) -> Result<(), Error>;
    /// Writes the contents of |buffer| to the device at |offset|.
    async fn write(&self, offset: u64, buffer: &Buffer<'_>) -> Result<(), Error>;
    /// Closes and consumes the block device.
    async fn close(self) -> Result<(), Error>;
}
