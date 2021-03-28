// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::device::buffer::{Buffer, BufferRef, MutableBufferRef},
    anyhow::Error,
    async_trait::async_trait,
};

pub mod buffer;
pub mod buffer_allocator;

#[cfg(target_os = "fuchsia")]
pub mod block_device;

#[cfg(target_family = "unix")]
pub mod file_backed_device;

#[async_trait]
/// Device is an abstract representation of an underlying block device.
pub trait Device: Send + Sync {
    /// Allocates a transfer buffer of at least |size| bytes for doing I/O with the device.
    /// The actual size of the buffer will be rounded up to a block-aligned size.
    fn allocate_buffer(&self, size: usize) -> Buffer<'_>;
    /// Returns the block size of the device. Buffers are aligned to block-aligned chunks.
    fn block_size(&self) -> u32;
    /// Returns the number of blocks of the device.
    // TODO(jfsulliv): Should this be async and go query the underlying device?
    fn block_count(&self) -> u64;
    /// Returns the size in bytes of the device.
    fn size(&self) -> usize {
        self.block_size() as usize * self.block_count() as usize
    }
    /// Fills |buffer| with blocks read from |offset|.
    async fn read(&self, offset: u64, buffer: MutableBufferRef<'_>) -> Result<(), Error>;
    /// Writes the contents of |buffer| to the device at |offset|.
    async fn write(&self, offset: u64, buffer: BufferRef<'_>) -> Result<(), Error>;
    /// Closes and consumes the block device.
    async fn close(self) -> Result<(), Error>;
}
