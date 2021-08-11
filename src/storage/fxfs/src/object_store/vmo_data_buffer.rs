// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::{data_buffer::DataBuffer, store_object_handle::round_up},
    fuchsia_zircon::{self as zx},
    std::ops::Range,
    storage_device::buffer::{BufferRef, MutableBufferRef},
};

/// A DataBuffer implementation backed by a VMO.
pub struct VmoDataBuffer(zx::Vmo);

impl VmoDataBuffer {
    pub fn new(size: u64) -> Self {
        let vmo = zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, size).unwrap();
        vmo.set_content_size(&size).unwrap();
        Self(vmo)
    }

    pub fn vmo(&self) -> &zx::Vmo {
        &self.0
    }
}

impl From<zx::Vmo> for VmoDataBuffer {
    fn from(vmo: zx::Vmo) -> Self {
        Self(vmo)
    }
}

impl DataBuffer for VmoDataBuffer {
    fn read(&self, offset: u64, mut buf: MutableBufferRef<'_>) {
        assert!(
            offset as usize + buf.len() <= self.0.get_content_size().unwrap() as usize,
            "offset: {} buf_len: {} inner.size: {}",
            offset,
            buf.len(),
            self.0.get_content_size().unwrap()
        );
        self.0.read(buf.as_mut_slice(), offset).unwrap();
    }
    fn write(&self, offset: u64, buf: BufferRef<'_>) {
        assert!(
            offset as usize + buf.len() <= self.0.get_content_size().unwrap() as usize,
            "offset: {} buf_len: {} inner.size: {}",
            offset,
            buf.len(),
            self.0.get_content_size().unwrap()
        );
        self.0.write(buf.as_slice(), offset).unwrap();
    }
    fn size(&self) -> u64 {
        self.0.get_content_size().unwrap()
    }
    fn resize(&self, size: u64) {
        let old_size = self.0.get_content_size().unwrap();
        let aligned_size = round_up(size, zx::system_get_page_size()).unwrap();
        self.0.set_size(aligned_size).unwrap();
        self.0.set_content_size(&size).unwrap();
        if size < old_size && aligned_size > size {
            self.0.op_range(zx::VmoOp::ZERO, size, aligned_size - size).unwrap();
        }
    }
    fn mark_unused(&self, range: Range<u64>) {
        if range.end == range.start {
            return;
        }
        self.0.op_range(zx::VmoOp::DECOMMIT, range.start, range.end - range.start).unwrap();
    }
    fn zero(&self, range: Range<u64>) {
        if range.end == range.start {
            return;
        }
        self.0.op_range(zx::VmoOp::ZERO, range.start, range.end - range.start).unwrap();
    }
}
