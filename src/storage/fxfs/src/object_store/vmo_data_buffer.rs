// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::{data_buffer::DataBuffer, store_object_handle::round_up},
    fuchsia_zircon::{self as zx},
    std::ops::Range,
    std::sync::Mutex,
    storage_device::buffer::{BufferRef, MutableBufferRef},
};

/// A DataBuffer implementation backed by a VMO.
pub struct VmoDataBuffer(Mutex<zx::Vmo>);

impl VmoDataBuffer {
    pub fn new(size: usize) -> Self {
        let vmo = zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, size as u64).unwrap();
        vmo.set_content_size(&(size as u64)).unwrap();
        Self(Mutex::new(vmo))
    }
}

impl DataBuffer for VmoDataBuffer {
    fn read(&self, offset: u64, mut buf: MutableBufferRef<'_>) {
        let vmo = self.0.lock().unwrap();
        assert!(
            offset as usize + buf.len() <= vmo.get_content_size().unwrap() as usize,
            "offset: {} buf_len: {} inner.size: {}",
            offset,
            buf.len(),
            vmo.get_content_size().unwrap()
        );
        vmo.read(buf.as_mut_slice(), offset).unwrap();
    }
    fn write(&self, offset: u64, buf: BufferRef<'_>) {
        let vmo = self.0.lock().unwrap();
        assert!(
            offset as usize + buf.len() <= vmo.get_content_size().unwrap() as usize,
            "offset: {} buf_len: {} inner.size: {}",
            offset,
            buf.len(),
            vmo.get_content_size().unwrap()
        );
        vmo.write(buf.as_slice(), offset).unwrap();
    }
    fn size(&self) -> usize {
        self.0.lock().unwrap().get_content_size().unwrap() as usize
    }
    fn resize(&self, size: usize) {
        let vmo = self.0.lock().unwrap();
        let old_size = vmo.get_content_size().unwrap() as u64;
        let aligned_size = round_up(size as u64, zx::system_get_page_size() as u64).unwrap();
        vmo.set_size(aligned_size).unwrap();
        vmo.set_content_size(&(size as u64)).unwrap();
        if (size as u64) < old_size && aligned_size > size as u64 {
            vmo.op_range(zx::VmoOp::ZERO, size as u64, aligned_size - size as u64).unwrap();
        }
    }
    fn mark_unused(&self, range: Range<u64>) {
        if range.end == range.start {
            return;
        }
        let vmo = self.0.lock().unwrap();
        vmo.op_range(zx::VmoOp::DECOMMIT, range.start, range.end - range.start).unwrap();
    }
    fn zero(&self, range: Range<u64>) {
        if range.end == range.start {
            return;
        }
        let vmo = self.0.lock().unwrap();
        vmo.op_range(zx::VmoOp::ZERO, range.start, range.end - range.start).unwrap();
    }
}
