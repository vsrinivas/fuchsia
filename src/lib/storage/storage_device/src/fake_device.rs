// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        buffer::{Buffer, BufferRef, MutableBufferRef},
        buffer_allocator::{BufferAllocator, MemBufferSource},
        Device,
    },
    anyhow::{ensure, Error},
    async_trait::async_trait,
    std::sync::{
        atomic::{AtomicBool, Ordering},
        Mutex,
    },
};

/// A Device backed by a memory buffer.
pub struct FakeDevice {
    allocator: BufferAllocator,
    data: Mutex<Vec<u8>>,
    closed: AtomicBool,
}

const TRANSFER_HEAP_SIZE: usize = 16 * 1024 * 1024;

impl FakeDevice {
    pub fn new(block_count: u64, block_size: u32) -> Self {
        let allocator = BufferAllocator::new(
            block_size as usize,
            Box::new(MemBufferSource::new(TRANSFER_HEAP_SIZE)),
        );
        Self {
            allocator,
            data: Mutex::new(vec![0 as u8; block_count as usize * block_size as usize]),
            closed: AtomicBool::new(false),
        }
    }
}

#[async_trait]
impl Device for FakeDevice {
    fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        assert!(!self.closed.load(Ordering::Relaxed));
        self.allocator.allocate_buffer(size)
    }

    fn block_size(&self) -> u32 {
        self.allocator.block_size() as u32
    }

    fn block_count(&self) -> u64 {
        self.data.lock().unwrap().len() as u64 / self.block_size() as u64
    }

    async fn read(&self, offset: u64, mut buffer: MutableBufferRef<'_>) -> Result<(), Error> {
        ensure!(!self.closed.load(Ordering::Relaxed));
        let offset = offset as usize;
        assert_eq!(offset % self.allocator.block_size(), 0);
        let data = self.data.lock().unwrap();
        let size = buffer.len();
        assert!(
            offset + size <= data.len(),
            "offset: {} len: {} data.len: {}",
            offset,
            size,
            data.len()
        );
        buffer.as_mut_slice().copy_from_slice(&data[offset..offset + size]);
        Ok(())
    }

    async fn write(&self, offset: u64, buffer: BufferRef<'_>) -> Result<(), Error> {
        ensure!(!self.closed.load(Ordering::Relaxed));
        let offset = offset as usize;
        assert_eq!(offset % self.allocator.block_size(), 0);
        let mut data = self.data.lock().unwrap();
        let size = buffer.len();
        assert!(
            offset + size <= data.len(),
            "offset: {} len: {} data.len: {}",
            offset,
            size,
            data.len()
        );
        data[offset..offset + size].copy_from_slice(buffer.as_slice());
        Ok(())
    }

    async fn close(&self) -> Result<(), Error> {
        self.closed.store(true, Ordering::Relaxed);
        Ok(())
    }

    async fn flush(&self) -> Result<(), Error> {
        Ok(())
    }

    fn reopen(&self) {
        self.closed.store(false, Ordering::Relaxed);
    }
}
