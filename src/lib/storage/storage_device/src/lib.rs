// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::buffer::{Buffer, BufferRef, MutableBufferRef},
    anyhow::{bail, Error},
    async_trait::async_trait,
    std::{ops::Deref, sync::Arc},
};

pub mod buffer;
pub mod buffer_allocator;

#[cfg(target_os = "fuchsia")]
pub mod block_device;

#[cfg(target_family = "unix")]
pub mod file_backed_device;

pub mod fake_device;

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
    fn size(&self) -> u64 {
        self.block_size() as u64 * self.block_count()
    }

    /// Fills |buffer| with blocks read from |offset|.
    async fn read(&self, offset: u64, buffer: MutableBufferRef<'_>) -> Result<(), Error>;

    /// Writes the contents of |buffer| to the device at |offset|.
    async fn write(&self, offset: u64, buffer: BufferRef<'_>) -> Result<(), Error>;

    /// Closes the block device. It is an error to continue using the device after this, but close
    /// itself is idempotent.
    async fn close(&self) -> Result<(), Error>;

    /// Flush the device.
    async fn flush(&self) -> Result<(), Error>;

    /// Reopens the device, making it usable again. (Only implemented for testing devices.)
    fn reopen(&self, _read_only: bool) {
        unreachable!();
    }
    /// Returns whether the device is read-only.
    fn is_read_only(&self) -> bool;

    /// Returns a snapshot of the device.
    fn snapshot(&self) -> Result<DeviceHolder, Error> {
        bail!("Not supported");
    }
}

// Arc<dyn Device> can easily be cloned and supports concurrent access, but sometimes exclusive
// access is required, in which case APIs should accept DeviceHolder.  It doesn't guarantee there
// aren't some users that hold an Arc<dyn Device> somewhere, but it does mean that something that
// accepts a DeviceHolder won't be sharing the device with something else that accepts a
// DeviceHolder.  For example, FxFilesystem accepts a DeviceHolder which means that you cannot
// create two FxFilesystem instances that are both sharing the same device.
pub struct DeviceHolder(Arc<dyn Device>);

impl DeviceHolder {
    pub fn new(device: impl Device + 'static) -> Self {
        DeviceHolder(Arc::new(device))
    }

    // Ensures there are no dangling references to the device. Useful for tests to ensure orderly
    // shutdown.
    pub fn ensure_unique(&self) {
        assert_eq!(Arc::strong_count(&self.0), 1);
    }
}

impl Deref for DeviceHolder {
    type Target = Arc<dyn Device>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}
