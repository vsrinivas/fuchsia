// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    std::{convert::TryInto, ops::Range, sync::Mutex},
    storage_device::buffer::{BufferRef, MutableBufferRef},
};

#[cfg(target_os = "fuchsia")]
use crate::object_store::vmo_data_buffer::VmoDataBuffer;

pub trait DataBufferFactory {
    fn create_data_buffer(&self, object_id: u64, initial_size: u64) -> NativeDataBuffer;
}

/// A readable, writable memory buffer that is not necessarily mapped into memory.
/// Mainly serves as a portable abstraction over a VMO (see VmoDataBuffer).
pub trait DataBuffer: Send + Sync {
    fn read(&self, offset: u64, buf: MutableBufferRef<'_>);
    fn write(&self, offset: u64, buf: BufferRef<'_>);
    fn size(&self) -> u64;
    fn resize(&self, size: u64);
    /// Marks |range| as unused, permitting its memory to be reclaimed.  Reading from the region
    /// should return zeroes.  The range must be page-aligned.
    fn mark_unused(&self, range: Range<u64>);
    /// Zeroes |range|.  Prefer using |mark_unused| for aligned ranges.
    fn zero(&self, range: Range<u64>);
}

#[cfg(target_os = "fuchsia")]
pub type NativeDataBuffer = VmoDataBuffer;

#[cfg(not(target_os = "fuchsia"))]
pub type NativeDataBuffer = MemDataBuffer;

/// A default implementation of a DataBuffer.
pub struct MemDataBuffer(Mutex<Vec<u8>>);

impl MemDataBuffer {
    pub fn new(size: u64) -> Self {
        Self(Mutex::new(vec![0u8; size.try_into().unwrap()]))
    }
}

impl DataBuffer for MemDataBuffer {
    fn read(&self, offset: u64, mut buf: MutableBufferRef<'_>) {
        let data = self.0.lock().unwrap();
        let len = buf.len();
        buf.as_mut_slice().copy_from_slice(&data[offset as usize..offset as usize + len]);
    }
    fn write(&self, offset: u64, buf: BufferRef<'_>) {
        let mut data = self.0.lock().unwrap();
        data[offset as usize..offset as usize + buf.len()].copy_from_slice(buf.as_slice());
    }
    fn size(&self) -> u64 {
        self.0.lock().unwrap().len() as u64
    }
    fn resize(&self, size: u64) {
        let mut data = self.0.lock().unwrap();
        data.resize(size.try_into().unwrap(), 0u8);
    }
    fn mark_unused(&self, range: Range<u64>) {
        self.zero(range);
    }
    fn zero(&self, range: Range<u64>) {
        let mut data = self.0.lock().unwrap();
        data[range.start as usize..range.end as usize].fill(0u8);
    }
}
